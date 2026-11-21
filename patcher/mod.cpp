#include "mod.h"

#include "bfevfl.h"
#include "byml.h"
#include "bytes.h"
#include "compression.h"
#include "game_file.h"
#include "msbt.h"
#include "new_actor.h"
#include "program_patch.h"
#include "sarc.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

static uint64_t ParseHash(const nlohmann::json& json) {
    const std::string text = json.get<std::string>();
    uint64_t hash = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), hash);
    if (error != std::errc() || end != text.data() + text.size()) {
        throw std::runtime_error(std::format("hash \"{}\" is not a 64-bit decimal number", text));
    }
    return hash;
}

static PlannedChest ParseChest(const nlohmann::json& chest) {
    const PlannedChest planned{ .hash = ParseHash(chest.at("hash")),
                                .files = chest.at("files").get<std::vector<std::string>>(),
                                .dropActor = chest.at("drop_actor").get<std::string>() };
    if (planned.files.empty() || planned.dropActor.empty()) {
        throw std::runtime_error("files and drop_actor must not be empty");
    }
    return planned;
}

static PlannedPresence ParsePresence(const nlohmann::json& presence) {
    PlannedPresence planned{ .actor = { .file = presence.at("file").get<std::string>(),
                                        .hash = ParseHash(presence.at("hash")) },
                             .flag = presence.at("flag").get<std::string>() };
    if (planned.actor.file.empty() || planned.flag.empty()) {
        throw std::runtime_error("file and flag must not be empty");
    }
    return planned;
}

static PlannedEventText ParseEventText(const nlohmann::json& text) {
    PlannedEventText planned{ .flow = text.at("flow").get<std::string>(),
                              .event = text.at("event").get<std::string>(),
                              .param = text.at("param").get<std::string>(),
                              .text = text.at("text").get<std::string>() };
    if (planned.text.empty()) {
        throw std::runtime_error("text must not be empty");
    }
    return planned;
}

static PlannedEventNumber ParseEventNumber(const nlohmann::json& number) {
    const nlohmann::json& value = number.at("value");
    if (!value.is_number_integer() || value < std::numeric_limits<int32_t>::min() ||
        value > std::numeric_limits<int32_t>::max()) {
        throw std::runtime_error("value must be a whole number");
    }
    return { .flow = number.at("flow").get<std::string>(),
             .event = number.at("event").get<std::string>(),
             .param = number.at("param").get<std::string>(),
             .value = value.get<int32_t>() };
}

static PlannedShopSlot ParseShopSlot(const nlohmann::json& slot) {
    const nlohmann::json& price = slot.at("price");
    if (!price.is_number_unsigned() || price > std::numeric_limits<int32_t>::max()) {
        throw std::runtime_error("price must be a whole number of rupees");
    }
    PlannedShopSlot planned{ .slot = slot.at("slot").get<uint32_t>(),
                             .item = slot.at("item").get<std::string>(),
                             .price = price.get<int32_t>() };
    for (const nlohmann::json& keeper : slot.at("shopkeepers")) {
        planned.shopkeepers.push_back(
            { .actor = keeper.at("actor").get<std::string>(), .goodsList = keeper.at("goods_list").get<std::string>() });
    }
    for (const nlohmann::json& shelf : slot.at("shelves")) {
        planned.shelves.push_back({ .file = shelf.at("file").get<std::string>(), .hash = ParseHash(shelf.at("hash")) });
    }
    if (planned.shopkeepers.empty() || planned.shelves.empty() || planned.item.empty()) {
        throw std::runtime_error("shopkeepers, shelves and item must not be empty");
    }
    return planned;
}

static PlannedActorItem ParseActorItem(const nlohmann::json& entry) {
    PlannedActorItem planned{ .hash = ParseHash(entry.at("hash")),
                              .files = entry.at("files").get<std::vector<std::string>>(),
                              .item = entry.at("item").get<std::string>() };
    if (planned.files.empty() || planned.item.empty()) {
        throw std::runtime_error("files and item must not be empty");
    }
    return planned;
}

static std::string Name(std::string_view flow, std::string_view event, std::string_view param) {
    return std::format("{} {} in {}", event, param, flow);
}

// A rumour opens with the stones' colour, their smaller text and code 3:0, and sets the size and colour again after a
// page break, as the game's own rumours do in every language.
constexpr std::string_view RumourOpening = "{color:1}{size:80}{3:0:0f0000}";
constexpr std::string_view RumourPageReset = "{size:80}{color:1}";

// A table of remake/ built into the program: each row's fields, after the header given. Throws when the header is
// another, the last line has no newline, or a row has another number of fields.
static std::vector<std::vector<std::string_view>> ReadTable(std::string_view name, std::string_view table,
                                                            std::string_view header) {
    if (!table.starts_with(std::format("{}\n", header)) || !table.ends_with('\n')) {
        throw std::runtime_error(
            std::format("{}: the header is not \"{}\", or the last line has no newline", name, header));
    }
    const size_t fields = std::ranges::count(header, '\t') + 1;
    std::vector<std::vector<std::string_view>> rows;
    for (size_t start = header.size() + 1; start < table.size(); start = table.find('\n', start) + 1) {
        std::vector<std::string_view>& row = rows.emplace_back();
        for (const auto field : std::views::split(table.substr(start, table.find('\n', start) - start), '\t')) {
            row.emplace_back(field.begin(), field.end());
        }
        if (row.size() != fields) {
            throw std::runtime_error(std::format("{} line {}: wrong number of fields", name, rows.size() + 1));
        }
    }
    return rows;
}

static constexpr char stoneTable[] = {
#embed "../remake/stones.tsv"
};

static std::set<std::string, std::less<>> PairedStones() {
    std::set<std::string, std::less<>> labels;
    for (const auto& row : ReadTable("remake/stones.tsv", { stoneTable, sizeof stoneTable }, "label\tlocation")) {
        labels.emplace(row[0]);
    }
    return labels;
}

static PlannedStoneText ParseStoneText(const nlohmann::json& stone) {
    static const std::set<std::string, std::less<>> paired = PairedStones();
    const std::string label = stone.at("label").get<std::string>();
    const std::string words = stone.at("text").get<std::string>();
    if (label.empty() || words.empty()) {
        throw std::runtime_error("label and text must not be empty");
    }
    if (!paired.contains(label)) {
        throw std::runtime_error(std::format("{} is not a stone of remake/stones.tsv", label));
    }
    std::string text = std::string(RumourOpening) + words;
    for (size_t page = text.find(msbt::PageTag); page != std::string::npos; page = text.find(msbt::PageTag, page + 1)) {
        text.insert(page + msbt::PageTag.size(), RumourPageReset);
    }
    return { .label = label, .text = msbt::Encode(text) };
}

static PlannedMessageText ParseMessageText(const nlohmann::json& message) {
    PlannedMessageText planned{ .file = message.at("file").get<std::string>(),
                                .label = message.at("label").get<std::string>(),
                                .text = message.at("text").get<std::string>() };
    if (planned.file.empty() || planned.label.empty() || planned.text.empty()) {
        throw std::runtime_error("file, label and text must not be empty");
    }
    planned.text = msbt::Encode(planned.text);
    return planned;
}

Plan ParsePlan(const nlohmann::json& json) {
    const auto chests = json.at("chests").get<std::vector<nlohmann::json>>();
    const auto texts = json.at("event_texts").get<std::vector<nlohmann::json>>();
    const auto numbers = json.at("event_numbers").get<std::vector<nlohmann::json>>();
    Plan plan;
    for (const auto& [name, value] : json.at("start_values").get<std::map<std::string, nlohmann::json>>()) {
        if (value.is_boolean()) {
            plan.startValues.emplace(name, value.get<bool>());
        } else if (value.is_number_integer() && value >= std::numeric_limits<int32_t>::min() &&
                   value <= std::numeric_limits<int32_t>::max()) {
            plan.startValues.emplace(name, value.get<int32_t>());
        } else {
            throw std::runtime_error(std::format("start_values: \"{}\" is not true, false or a whole number", name));
        }
    }
    const auto stones = json.at("stone_texts").get<std::vector<nlohmann::json>>();
    for (size_t i = 0; i < chests.size(); ++i) {
        try {
            plan.chests.push_back(ParseChest(chests[i]));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("chests[{}]: {}", i, e.what()));
        }
    }
    std::set<std::tuple<std::string, std::string, std::string>> named;
    for (size_t i = 0; i < texts.size(); ++i) {
        try {
            const PlannedEventText& text = plan.eventTexts.emplace_back(ParseEventText(texts[i]));
            if (!named.emplace(text.flow, text.event, text.param).second) {
                throw std::runtime_error(std::format("{} is listed twice", Name(text.flow, text.event, text.param)));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event_texts[{}]: {}", i, e.what()));
        }
    }
    for (size_t i = 0; i < numbers.size(); ++i) {
        try {
            const PlannedEventNumber& number = plan.eventNumbers.emplace_back(ParseEventNumber(numbers[i]));
            if (!named.emplace(number.flow, number.event, number.param).second) {
                throw std::runtime_error(
                    std::format("{} is listed twice", Name(number.flow, number.event, number.param)));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("event_numbers[{}]: {}", i, e.what()));
        }
    }
    std::set<std::string> labels;
    for (size_t i = 0; i < stones.size(); ++i) {
        try {
            const PlannedStoneText& stone = plan.stoneTexts.emplace_back(ParseStoneText(stones[i]));
            if (!labels.insert(stone.label).second) {
                throw std::runtime_error(std::format("{} is listed twice", stone.label));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("stone_texts[{}]: {}", i, e.what()));
        }
    }
    const auto messages = json.at("message_texts").get<std::vector<nlohmann::json>>();
    std::set<std::pair<std::string, std::string>> messageLabels;
    for (size_t i = 0; i < messages.size(); ++i) {
        try {
            const PlannedMessageText& message = plan.messageTexts.emplace_back(ParseMessageText(messages[i]));
            if (!messageLabels.emplace(message.file, message.label).second) {
                throw std::runtime_error(std::format("{} of {} is listed twice", message.label, message.file));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("message_texts[{}]: {}", i, e.what()));
        }
    }
    const auto shops = json.at("shops").get<std::vector<nlohmann::json>>();
    for (size_t i = 0; i < shops.size(); ++i) {
        try {
            plan.shops.push_back(ParseShopSlot(shops[i]));
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("shops[{}]: {}", i, e.what()));
        }
    }
    plan.startItems = json.at("start_items").get<std::vector<std::string>>();
    for (const std::string& item : plan.startItems) {
        if (std::ranges::count(plan.startItems, item) > 1) {
            throw std::runtime_error(std::format("start_items: {} is listed twice", item));
        }
    }
    plan.midoCastleEntryRemoved = json.at("mido_castle_entry_removed").get<bool>();
    plan.openingSkipped = json.at("opening_skipped").get<bool>();
    plan.cutscenesSkipped = json.at("cutscenes_skipped").get<bool>();
    for (const auto& [key, list] : { std::pair{ "skulltulas", &plan.skulltulas },
                                     std::pair{ "loose_items", &plan.looseItems } }) {
        const auto entries = json.at(key).get<std::vector<nlohmann::json>>();
        for (size_t i = 0; i < entries.size(); ++i) {
            try {
                list->push_back(ParseActorItem(entries[i]));
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("{}[{}]: {}", key, i, e.what()));
            }
        }
    }
    const auto presences = json.at("presences").get<std::vector<nlohmann::json>>();
    std::set<std::pair<std::string, uint64_t>> placed;
    for (size_t i = 0; i < presences.size(); ++i) {
        try {
            const PlannedPresence& presence = plan.presences.emplace_back(ParsePresence(presences[i]));
            if (!placed.emplace(presence.actor.file, presence.actor.hash).second) {
                throw std::runtime_error(
                    std::format("actor {} in {} is listed twice", presence.actor.hash, presence.actor.file));
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("presences[{}]: {}", i, e.what()));
        }
    }
    return plan;
}

using byml::Find;
using byml::FindOrAdd;

static byml::Node* FindHash(byml::Node& array, uint64_t hash) {
    const auto found = std::ranges::find_if(array.array, [&](byml::Node& item) {
        const byml::Node* itemHash = Find(item, "Hash");
        return itemHash != nullptr && itemHash->scalar == hash;
    });
    return found == array.array.end() ? nullptr : &*found;
}

static byml::Node& Actor(byml::Node& placement, uint64_t hash) {
    byml::Node* actors = Find(placement, "Actors");
    byml::Node* actor = actors == nullptr ? nullptr : FindHash(*actors, hash);
    if (actor == nullptr) {
        throw std::runtime_error("no actor in the file has this hash");
    }
    return *actor;
}

static bool SetGyaml(byml::Node& placed, const std::string& actor) {
    byml::Node* gyaml = Find(placed, "Gyaml");
    if (gyaml == nullptr || gyaml->type != byml::Type::String) {
        throw std::runtime_error("the actor has no Gyaml");
    }
    return std::exchange(gyaml->string, actor) != actor;
}

// Items whose own actor is a pickup that the game places loose. Fire Arrows are not one: their pickup's tag makes
// taking it move the lake's Fire Arrows quest on, so it stays only at the lake, where the game places it.
constexpr std::array<std::string_view, 2> LoosePickups = { "Obj_Heart_Container", "Obj_Heart_Piece" };

static constexpr char shopGoodsTable[] = {
#embed "../remake/shop_goods.tsv"
};

// How the model of an item sold from goods of its own stands in the pose a shop plays for it, in millimetres.
struct GoodsModel {
    int32_t bottom;
    int32_t top;
    int32_t width;
    int32_t reach;
};

// A row of remake/shop_goods.tsv: the game's goods actor that sells the item, or the model of goods of its own.
using ShopGoods = std::variant<std::string, GoodsModel>;

static const ShopGoods& Goods(std::string_view item) {
    static const std::map<std::string, ShopGoods, std::less<>> rows = [] {
        std::map<std::string, ShopGoods, std::less<>> rows;
        for (const auto& row : ReadTable("remake/shop_goods.tsv", { shopGoodsTable, sizeof shopGoodsTable },
                                         "item\tgoods\tpose\tbottom\ttop\twidth\treach")) {
            if (!row[1].empty()) {
                rows.emplace(row[0], std::string(row[1]));
                continue;
            }
            std::array<int32_t, 4> millimetres{};
            for (size_t i = 0; i < millimetres.size(); ++i) {
                const std::string_view cell = row[3 + i];
                const auto [end, error] = std::from_chars(cell.data(), cell.data() + cell.size(), millimetres[i]);
                if (error != std::errc() || end != cell.data() + cell.size()) {
                    throw std::runtime_error(std::format("remake/shop_goods.tsv: {} has no model", row[0]));
                }
            }
            const auto [bottom, top, width, reach] = millimetres;
            rows.emplace(row[0], GoodsModel{ bottom, top, width, reach });
        }
        return rows;
    }();
    const auto goods = rows.find(item);
    if (goods == rows.end()) {
        throw std::runtime_error(std::format("remake/shop_goods.tsv has no row for {}", item));
    }
    return goods->second;
}

// The tallest of the game's own goods on a shop's shelf, the potions, and the farthest any reaches from the upright
// through its origin, the bombchus, in millimetres, as their shop poses stand them.
constexpr double TallestGoods = 561;
constexpr double LongestReach = 395;
// The smallest of those goods, the heart, by the larger of its height and its width, in millimetres, and the most the
// game raises any of its goods for a shelf, the masks.
constexpr double SmallestGoods = 318;
constexpr double MostRaised = 1.4;

// The game's goods rest the lowest point of their pose on their shelf's origin, so the shelf rises by what hangs below
// it. Every shelf stands upright.
static void StandOnShelf(byml::Node& shelf, const GoodsModel& model) {
    const double height = model.top - model.bottom;
    const double raised = std::max(1.0, std::min(MostRaised, SmallestGoods / std::max<double>(height, model.width)));
    const double scale = std::min({ TallestGoods / height, LongestReach / model.reach, raised });
    byml::Node* translate = Find(shelf, "Translate");
    if (translate == nullptr || translate->array.size() != 3) {
        throw std::runtime_error("the shelf has no Translate");
    }
    byml::Node& y = translate->array[1];
    y.scalar = std::bit_cast<uint32_t>(float(std::bit_cast<float>(uint32_t(y.scalar)) - scale * model.bottom / 1000));
    if (scale != 1) {
        const byml::Node size{ .type = byml::Type::Float, .scalar = std::bit_cast<uint32_t>(float(scale)) };
        FindOrAdd(shelf, "Scale", byml::Type::Array).array = { size, size, size };
    }
}

constexpr std::string_view QuantitySign = "Shop_Board_Bundle_";

// A row with no BundleActorNum gives one thing, as the token's and the Deku Seed's do. A rupee's row holds its worth
// there, which is no count of things on the shelf.
static uint32_t Quantity(byml::Node& pouchRow) {
    const byml::Node* bundle = Find(pouchRow, "BundleActorNum");
    const byml::Node* category = Find(pouchRow, "PouchCategory");
    return bundle == nullptr || (category != nullptr && category->string == "Rupee") ? 1 : uint32_t(bundle->scalar);
}

// Takes the actors out of the scene with every mention of them, wherever the file holds it: an entry of any list that
// is one of their hashes, or a dict that holds one directly, such as the actor or a link to it. A list emptied so goes
// from its dict, as the game writes no empty list of links.
static void Unplace(byml::Node& node, const std::vector<uint64_t>& hashes) {
    const auto isHash = [&](const byml::Node& value) {
        return value.type == byml::Type::UInt64 && std::ranges::find(hashes, value.scalar) != hashes.end();
    };
    if (node.type == byml::Type::Array) {
        std::erase_if(node.array, [&](const byml::Node& item) {
            return isHash(item) || std::ranges::any_of(item.dict, isHash, &byml::Entry::value);
        });
        for (byml::Node& item : node.array) {
            Unplace(item, hashes);
        }
    } else if (node.type == byml::Type::Dict) {
        std::vector<std::string> emptied;
        for (byml::Entry& entry : node.dict) {
            const bool held = !entry.value.array.empty();
            Unplace(entry.value, hashes);
            if (held && entry.value.array.empty()) {
                emptied.push_back(entry.key);
            }
        }
        std::erase_if(node.dict, [&](const byml::Entry& entry) {
            return std::ranges::find(emptied, entry.key) != emptied.end();
        });
    }
}

// The game's single goods have no quantity sign. Returns whether the scene changed.
static bool ShowQuantity(const GameFile& game, byml::Node& placement, uint64_t shelf, uint32_t quantity) {
    std::vector<uint64_t> signs;
    if (byml::Node* links = Find(Actor(placement, shelf), "Links")) {
        for (byml::Node& link : links->array) {
            const uint64_t linked = Find(link, "Dst")->scalar;
            if (Find(Actor(placement, linked), "Gyaml")->string.starts_with(QuantitySign)) {
                signs.push_back(linked);
            }
        }
    }
    if (signs.empty()) {
        return false;
    }
    if (quantity > 1) {
        const std::string sign = std::format("{}{:02}", QuantitySign, quantity);
        if (!game.Files().contains(std::format("Pack/Actor/{}.pack.zs", sign))) {
            throw std::runtime_error(std::format("the game has no quantity sign for {}", quantity));
        }
        bool changed = false;
        for (const uint64_t hash : signs) {
            std::string& gyaml = Find(Actor(placement, hash), "Gyaml")->string;
            changed |= gyaml != sign;
            gyaml = sign;
        }
        return changed;
    }
    Unplace(placement, signs);
    return true;
}

// A shop sells the game's goods at their base price in this table, the row's BuyingPrice, plus the slot's
// PriceOffset.
static int32_t BasePrice(byml::Node& pouchActorInfo, const std::string& goodsActor) {
    byml::Node* price = Find(Row(pouchActorInfo, goodsActor), "BuyingPrice");
    if (price == nullptr || price->type != byml::Type::Int) {
        throw std::runtime_error(std::format("{} has no BuyingPrice for {}", PouchActorInfoPath, goodsActor));
    }
    return int32_t(uint32_t(price->scalar));
}

// Each shopkeeper's actor pack holds its own copy of the goods list it sells from.
struct ShopPack {
    sarc::Files files;
    std::map<std::string, byml::Document> goodsLists;

    byml::Node& GoodsList(const std::string& goodsList) {
        const std::string name = std::format("Component/ShopParam/{}.game__component__ShopParam.bgyml", goodsList);
        auto list = goodsLists.find(name);
        if (list == goodsLists.end()) {
            const auto file = files.find(name);
            if (file == files.end()) {
                throw std::runtime_error(std::format("the shopkeeper's pack has no {}", name));
            }
            list = goodsLists.emplace(name, byml::Parse(file->second)).first;
        }
        return list->second.root;
    }
};

// A gate on the slot's release and a discount were for the game's own goods, so they take the forms of a slot with
// neither.
static void SetGoods(byml::Node& goodsList, uint32_t slot, const std::string& goodsActor, int32_t priceOffset) {
    byml::Node* list = Find(goodsList, "GoodsList");
    if (list == nullptr || slot >= list->array.size()) {
        throw std::runtime_error(std::format("the goods list has no slot {}", slot));
    }
    byml::Node& goods = list->array[slot];
    FindOrAdd(goods, "Actor", byml::Type::String).string = WorkPath(goodsActor);
    FindOrAdd(goods, "PriceOffset", byml::Type::Int).scalar = uint32_t(priceOffset);
    FindOrAdd(goods, "StockNum", byml::Type::Int).scalar = 1;
    if (byml::Node* release = Find(goods, "Release")) {
        *release = { .type = byml::Type::Dict,
                     .dict = { { .key = "IsCheckReleaseGameData", .value = { .type = byml::Type::Bool } },
                               { .key = "ReleaseGameData", .value = { .type = byml::Type::String } } } };
    }
    if (byml::Node* discounts = Find(goods, "ConditionalPriceOffsetList")) {
        discounts->array.clear();
    }
}

// A chest gives its Dynamic.Drop__DropActor. One without it gives a refill of what Link is lowest on, so the item is
// added, in key order as the game's dicts are. One with a Drop__DropActor_Substitute gives that instead to Link who
// owns its Drop__CheckActor, so the substitute takes the item too. Returns whether the actor changed.
static bool SetItem(byml::Node& chest, const std::string& item) {
    byml::Node* dynamic = Find(chest, "Dynamic");
    if (dynamic == nullptr) {
        throw std::runtime_error("the chest's actor has no Dynamic values");
    }
    const std::string dropActor = "Drop__DropActor";
    FindOrAdd(*dynamic, dropActor, byml::Type::String);
    bool changed = false;
    for (const std::string& key : { dropActor, std::string("Drop__DropActor_Substitute") }) {
        byml::Node* value = Find(*dynamic, key);
        if (value == nullptr) {
            continue;
        }
        if (value->type != byml::Type::String) {
            throw std::runtime_error(std::format("the chest's Dynamic.{} is not a string", key));
        }
        changed |= value->string != item;
        value->string = item;
    }
    return changed;
}

// A Gold Skulltula drops its Dynamic.Drop__DropActor in place of the token its drop table names.
static bool SetDrop(byml::Node& skulltula, const std::string& dropActor) {
    const byml::Node* gyaml = Find(skulltula, "Gyaml");
    if (gyaml == nullptr || gyaml->string != "Enemy_Skulltula_Gold") {
        throw std::runtime_error("the actor is not a Gold Skulltula");
    }
    byml::Node& dynamic = FindOrAdd(skulltula, "Dynamic", byml::Type::Dict);
    byml::Node& drop = FindOrAdd(dynamic, "Drop__DropActor", byml::Type::String);
    if (drop.type != byml::Type::String) {
        throw std::runtime_error("the skulltula's Dynamic.Drop__DropActor is not a string");
    }
    return std::exchange(drop.string, dropActor) != dropActor;
}

constexpr std::string_view TokenItem = "Item_Token_Gold";

// The game reads the update's size table and ignores the base game's.
constexpr std::string_view SizeTablePath = "System/Resource/ResourceSizeTable.Product.111.Nin_Ounce_NVN2.rsizetable.zs";

// "RESTBL", u32 version, u32 name length, u32 entry count, u32 name entry count, then the entries: the CRC-32 of a
// path without its ".zs", and a u32 size. Entries by name follow for paths whose CRC-32s collide, and those CRC-32s
// have no entry.
constexpr std::string_view SizeTableMagic = "RESTBL";
constexpr uint32_t SizeTableVersion = 1;
constexpr size_t SizeTableHeaderSize = 0x16;
constexpr size_t SizeTableEntrySize = 8;

// CRC-32 as zlib computes it.
static uint32_t Crc32(std::string_view data) {
    uint32_t crc = 0xFFFFFFFF;
    for (const char byte : data) {
        crc ^= uint8_t(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
    }
    return ~crc;
}

static std::string ReadSizeTable(GameFile& game) {
    try {
        std::string table = Decompress(game.Read(std::string(SizeTablePath)));
        if (!table.starts_with(SizeTableMagic) || ReadAt<uint32_t>(table, 0x06) != SizeTableVersion) {
            throw std::runtime_error(std::format("not a version {} size table", SizeTableVersion));
        }
        return table;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", SizeTablePath, e.what()));
    }
}

// The game's own size table entries are a file's decompressed size rounded up to a multiple of 32, plus an overhead
// for its kind of file.
constexpr size_t SizeAlignment = 32;
constexpr size_t PlacementOverhead = 264;
constexpr size_t EventFlowOverhead = 272;
constexpr size_t TextPackOverhead = 312;
constexpr size_t ActorPackOverhead = 312;
constexpr size_t ActorTableOverhead = 264;
constexpr size_t ActorInfoOverhead = 3064;

// Raises the size table entry of the file at this path if its decompressed size needs a larger one. Returns whether
// the entry changed. The game silently skips a file that is too large for its entry, and loads one that is smaller.
static bool RaiseSize(std::string& table, std::string_view path, size_t size, size_t overhead) {
    const uint32_t key = Crc32(path.substr(0, path.size() - std::strlen(".zs")));
    const size_t end = SizeTableHeaderSize + ReadAt<uint32_t>(table, 0x0E) * SizeTableEntrySize;
    for (size_t entry = SizeTableHeaderSize; entry < end; entry += SizeTableEntrySize) {
        if (ReadAt<uint32_t>(table, entry) == key) {
            const uint32_t value = uint32_t(AlignUp(size, SizeAlignment) + overhead);
            if (ReadAt<uint32_t>(table, entry + 4) >= value) {
                return false;
            }
            WriteAt(table, entry + 4, value);
            return true;
        }
    }
    throw std::runtime_error(std::format("{}: the size table has no entry for this file", path));
}

// Adds the entry of a file the game does not have, in the CRC-32 order the table keeps.
static void AddSize(std::string& table, std::string_view path, size_t size, size_t overhead) {
    const std::string_view name = path.substr(0, path.size() - std::strlen(".zs"));
    const uint32_t key = Crc32(name);
    const uint32_t count = ReadAt<uint32_t>(table, 0x0E);
    const size_t end = SizeTableHeaderSize + count * SizeTableEntrySize;
    const size_t nameSize = ReadAt<uint32_t>(table, 0x0A);
    for (size_t entry = end; entry < table.size(); entry += nameSize + 4) {
        const std::string_view named = std::string_view(table).substr(entry, nameSize);
        if (Crc32(named.substr(0, named.find('\0'))) == key) {
            throw std::runtime_error(std::format("{}: the CRC-32 of this file's path collides with another's", path));
        }
    }
    size_t entry = SizeTableHeaderSize;
    while (entry < end && ReadAt<uint32_t>(table, entry) < key) {
        entry += SizeTableEntrySize;
    }
    if (entry < end && ReadAt<uint32_t>(table, entry) == key) {
        throw std::runtime_error(std::format("{}: the size table already has an entry for this file", path));
    }
    std::string added(SizeTableEntrySize, '\0');
    WriteAt(added, 0, key);
    WriteAt(added, 4, uint32_t(AlignUp(size, SizeAlignment) + overhead));
    table.insert(entry, added);
    WriteAt(table, 0x0E, count + 1);
}

// Adds the row in __RowId order, the order of the game's actor tables.
static void AddRow(byml::Node& table, const byml::Node& row) {
    const auto at = std::ranges::lower_bound(table.array, RowId(row), {}, RowId);
    if (at != table.array.end() && RowId(*at) == RowId(row)) {
        throw std::runtime_error(std::format("the game already has an actor {}", RowId(row)));
    }
    table.array.insert(at, row);
}

// The Fairy Bow's and the Fairy Slingshot's rows, by the item's name and by the actor name the game counts each by,
// and the small container of each. The pouch add treats any row that lists actors to fill as a bag's: it raises the
// limit the row names and fills each listed actor to it. It asks for the list in the row of the name its caller gives
// and reads the rest from the row of the actor name. A shop asks for the container before it sells arrows or seeds.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> WeaponContainers{ {
    { "Item_Range_Bow", "Item_Quiver_Small" },
    { "Weapon_Range_Bow", "Item_Quiver_Small" },
    { "Item_Range_SlingShot", "Item_BulletBag_Small" },
    { "Weapon_Range_SlingShot", "Item_BulletBag_Small" },
} };

static void BringContainers(byml::Node& pouchActorInfo) {
    for (const auto& [weapon, container] : WeaponContainers) {
        byml::Node fills = Row(pouchActorInfo, container);
        byml::Node& row = Row(pouchActorInfo, weapon);
        for (const char* key :
             { "MaxNumGameData", "AfterExpansionMaxNum", "IsItemStockMaxIfGet", "ExpansionMaxNumActor" }) {
            const byml::Node* value = Find(fills, key);
            if (value == nullptr) {
                throw std::runtime_error(std::format("{}: {} has no {}", PouchActorInfoPath, container, key));
            }
            FindOrAdd(row, key, value->type) = *value;
        }
        FindOrAdd(row, "ExpansionMaxNumActor", byml::Type::String).string += "," + WorkPath(container);
    }
}

// A new game starts with the defaults this list gives every saved value.
constexpr std::string_view GameDataListPath = "GameData/GameDataList.Product.110.byml.zs";

// MurmurHash3 x86_32 with seed 0. The game data list keys each value by this hash of its name.
static uint32_t Murmur3(std::string_view name) {
    const auto mix = [](uint32_t k) { return std::rotl(k * 0xCC9E2D51, 15) * 0x1B873593; };
    const size_t blocks = name.size() / 4 * 4;
    uint32_t hash = 0;
    for (size_t i = 0; i < blocks; i += 4) {
        hash = std::rotl(hash ^ mix(ReadAt<uint32_t>(name, i)), 13) * 5 + 0xE6546B64;
    }
    uint32_t tail = 0;
    for (size_t i = blocks; i < name.size(); ++i) {
        tail |= uint32_t(uint8_t(name[i])) << 8 * (i - blocks);
    }
    hash ^= mix(tail) ^ uint32_t(name.size());
    hash = (hash ^ hash >> 16) * 0x85EBCA6B;
    hash = (hash ^ hash >> 13) * 0xC2B2AE35;
    return hash ^ hash >> 16;
}

// The game data list's table and node type for each kind of start value, in the order of StartValue's types.
static constexpr std::tuple<std::string_view, byml::Type, std::string_view> startValueKinds[] = {
    { "Bool", byml::Type::Bool, "a true-or-false value" },
    { "Int", byml::Type::Int, "a whole number" },
};

static byml::Node& DefaultValue(byml::Node& data, const std::string& name, const StartValue& value) {
    const auto& [kind, type, words] = startValueKinds[value.index()];
    byml::Node* values = Find(data, kind);
    byml::Node* entry = values == nullptr ? nullptr : FindHash(*values, Murmur3(name));
    byml::Node* defaultValue = entry == nullptr ? nullptr : Find(*entry, "DefaultValue");
    if (defaultValue == nullptr || defaultValue->type != type) {
        throw std::runtime_error(std::format("start value \"{}\" is not {} in the list", name, words));
    }
    return *defaultValue;
}

// The start values that give a new game the item as a find of it does: the item held, its get scene counted as played,
// and one in the pouch. Its row in PouchActorInfo must name no special path of the program (PouchSpecialDeal None), no
// ability (ExpansionAvility) and no limit (MaxNumGameData). Throws on any other row.
static std::map<std::string, StartValue> StartItemValues(byml::Node& row, const std::string& item) {
    const byml::Node* deal = Find(row, "PouchSpecialDeal");
    if (deal == nullptr || deal->string != "None") {
        throw std::runtime_error(std::format("its PouchSpecialDeal is {}, which a start does not write",
                                             deal == nullptr ? "missing" : deal->string));
    }
    for (const std::string_view key : { "ExpansionAvility", "MaxNumGameData" }) {
        if (const byml::Node* value = Find(row, key); value != nullptr && !value->string.empty()) {
            throw std::runtime_error(std::format("its {} is {}, which a start does not write", key, value->string));
        }
    }
    return { { "IsGet." + item, true },
             { "HavePlayedGetEvent." + item, true },
             { std::format("PouchData.{}.StockNum", item), 1 } };
}

// A new game starts with each start value as its default. Each count of a shop slot sold takes reset type 0 in place
// of the game's 2, the type that the game's data has a day change reset. A Bool's or an Int's default and an entry's
// reset type sit inside the entry, so the list keeps its size and still fits its size table entry.
static std::string GameDataList(GameFile& game, const std::string& dictionary,
                                const std::map<std::string, StartValue>& values, const std::set<std::string>& soldOnce) {
    try {
        byml::Document list = byml::Parse(Decompress(game.Read(std::string(GameDataListPath)), dictionary));
        byml::Node* data = Find(list.root, "Data");
        if (data == nullptr) {
            throw std::runtime_error("the list has no Data");
        }
        for (const auto& [name, value] : values) {
            byml::Node& defaultValue = DefaultValue(*data, name, value);
            const uint64_t scalar = std::visit([](auto v) { return uint64_t(uint32_t(v)); }, value);
            if (defaultValue.scalar == scalar) {
                throw std::runtime_error(std::format("start value \"{}\" is already {} by default", name,
                                                     std::visit([](auto v) { return std::format("{}", v); }, value)));
            }
            defaultValue.scalar = scalar;
        }
        for (const std::string& name : soldOnce) {
            byml::Node* ints = Find(*data, "Int");
            byml::Node* entry = ints == nullptr ? nullptr : FindHash(*ints, Murmur3(name));
            byml::Node* reset = entry == nullptr ? nullptr : Find(*entry, "ResetTypeValue");
            if (reset == nullptr || reset->type != byml::Type::Int) {
                throw std::runtime_error(std::format("\"{}\" is not a count with a reset type in the list", name));
            }
            reset->scalar = 0;
        }
        return Compress(byml::Write(list), dictionary);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", GameDataListPath, e.what()));
    }
}

// The hashes of the game data list's true-or-false values and expressions, the flags a presence can name.
static std::set<uint32_t> PresenceFlags(GameFile& game, const std::string& dictionary) {
    byml::Document list = byml::Parse(Decompress(game.Read(std::string(GameDataListPath)), dictionary));
    byml::Node* data = Find(list.root, "Data");
    std::set<uint32_t> flags;
    for (const std::string_view kind : { "Bool", "BoolExp" }) {
        byml::Node* values = data == nullptr ? nullptr : Find(*data, kind);
        if (values == nullptr) {
            throw std::runtime_error(std::format("{} has no {} values", GameDataListPath, kind));
        }
        for (byml::Node& value : values->array) {
            if (const byml::Node* hash = Find(value, "Hash"); hash != nullptr) {
                flags.insert(uint32_t(hash->scalar));
            }
        }
    }
    return flags;
}

// Link takes a bomb out of the bag only when the bomb's lift grade is at most his, as he lifts a rock. A bomb's grade
// is Low, which a child reaches only with Goron's Bracelet. At None, the bombchu's grade, a child with no strength
// takes a bomb out and breaks a wall with it. Each of these packs holds a copy of the bomb's lift file, and which copy
// the game loads is not known. In the three bomb flower packs the fruit's own lift file builds on this one and states
// Low itself, so a bomb flower still takes the bracelet's strength.
constexpr std::string_view BombLiftFile = "Component/LiftableParam/Item_Bomb.game__component__LiftableParam.bgyml";
constexpr std::array<std::string_view, 5> BombLiftPacks{
    "Pack/Actor/Item_Bomb.pack.zs",
    "Pack/Actor/Npc_Hylia_ImperialSoldier_Bomb.pack.zs",
    "Pack/Actor/Obj_BombFlower_A_01.pack.zs",
    "Pack/Actor/Obj_BombFlower_A_04.pack.zs",
    "Pack/Actor/Obj_BombFlower_Fruit_For_Break_Obj_DeathMtTrail_BrokenRock_A_01.pack.zs",
};

// The grade changes in place, so the pack keeps its size and still fits its size table entry.
static std::string BombLiftPack(GameFile& game, const std::string& dictionary, std::string_view path) {
    try {
        sarc::Files pack = sarc::Parse(Decompress(game.Read(std::string(path)), dictionary));
        const auto file = pack.find(std::string(BombLiftFile));
        if (file == pack.end()) {
            throw std::runtime_error(std::format("the pack has no {}", BombLiftFile));
        }
        byml::SetRootUInt(file->second, "LiftGrade", Murmur3("Low"), Murmur3("None"));
        return Compress(sarc::Write(pack, sarc::ActorPackAlignment), dictionary);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path, e.what()));
    }
}

// Once the castle quest is complete, the last entry of Mido's schedule sends him away from his post with a talk that
// never lets Link by, while the wall at the post stands as long as his blocker value holds. Without that entry, a Link
// who meets Zelda before Mido lets him by finds Mido at his post with his blocking talk, as before Zelda. Once Mido has
// let him by, the other entries hold as before, and Mido gives his lines for the forest's story instead of those for
// after the castle.
constexpr std::string_view MidoSchedule =
    "Component/AIScheduleParam/Npc_Kokiri_Mido_Child.game__component__AIScheduleParam.bgyml";
constexpr std::string_view MidoCastleCondition = "Goto_HyruleCastle_IsCompleted_Exp";

// The file shrinks, so it still fits its size table entry.
static std::string MidoScheduleWithoutCastleEntry(GameFile& game) {
    try {
        byml::Document schedule = byml::Parse(game.Read(std::string(MidoSchedule)));
        byml::Node* entries = Find(schedule.root, "ConditionalSchedule");
        byml::Node* condition =
            entries == nullptr || entries->array.empty() ? nullptr : Find(entries->array.back(), "ConditionFlag");
        if (condition == nullptr || condition->string != MidoCastleCondition) {
            throw std::runtime_error(std::format("the last entry is not the one for {}", MidoCastleCondition));
        }
        entries->array.pop_back();
        return byml::Write(schedule);
    } catch (const std::exception& e) { throw std::runtime_error(std::format("{}: {}", MidoSchedule, e.what())); }
}

// Adult Link becomes a child by putting the Master Sword back. This logic module takes the sword's interaction away
// while the expression it names is false, and the game names the Forest Temple's quest complete. Every adult has the
// Door of Time's quest complete, since the first pull of the sword completes it. The module names its expression once,
// in its string pool, and the file is stored uncompressed. The shorter name ends in NUL bytes where the longer one
// stood, so the module keeps its size and its size table entry.
constexpr std::string_view PedestalModule = "Logic/Market_Adult_Prefab_89da.logic.module.ainb";
constexpr std::string_view ForestTempleDone = "SageOfForest_IsCompleted_Exp";
constexpr std::string_view DoorOfTimeOpened = "DoorOfTime_IsCompleted_Exp";

static std::string OpenPedestal(GameFile& game) {
    std::string module = game.Read(std::string(PedestalModule));
    const std::string gameName = std::string(ForestTempleDone) + '\0';
    const size_t at = module.find(gameName);
    if (at == std::string::npos || module.find(gameName, at + 1) != std::string::npos) {
        throw std::runtime_error(std::format("{}: the module does not name {} once", PedestalModule, ForestTempleDone));
    }
    std::string name(DoorOfTimeOpened);
    name.resize(gameName.size(), '\0');
    module.replace(at, name.size(), name);
    return module;
}

// Before the Forest Temple, Sheik's schedule sends him to his AnchorAction1, 0.77 units in front of the Master Sword on
// its axis. The sword's "Return" prompt needs Link within 1.5 units in front of it, and after a pull adult Link appears
// 0.34 units from that spot. Sheik's own placement stands 1.39 units behind the sword. The anchor takes that x and
// keeps its height and z, so its position stays a container of its own and the file keeps its size. His lesson after
// the Forest Temple warps him to a spot of its own.
constexpr std::string_view TempleStatic = "Banc/Market/Market_Adult_Static.bcett.byml.zs";
constexpr uint64_t SheikHash = 2669233513522124871;
constexpr uint64_t SheikWaitHash = 3082534610608980032;
// The anchor's position as the game ships it, (136.871, 106.3536, -34.0), as the bits of its floats.
constexpr std::array<uint32_t, 3> SheikWaitShipped = { 0x4308DEFA, 0x42D4B50B, 0xC2080000 };

static void MoveSheikWaitBehindSword(byml::Node& placement) {
    byml::Node* wait = Find(Actor(placement, SheikWaitHash), "Translate");
    const bool shipped = wait != nullptr && wait->array.size() == SheikWaitShipped.size() &&
                         std::ranges::equal(wait->array, SheikWaitShipped, [](const byml::Node& value, uint32_t bits) {
                             return value.type == byml::Type::Float && value.scalar == bits;
                         });
    if (!shipped) {
        throw std::runtime_error("Sheik's AnchorAction1 is not where the game ships it");
    }
    const byml::Node* sheik = Find(Actor(placement, SheikHash), "Translate");
    if (sheik == nullptr || sheik->array.empty() || sheik->array[0].type != byml::Type::Float) {
        throw std::runtime_error("Sheik has no position");
    }
    wait->array[0] = sheik->array[0];
}

// The game reads the update's text pack of the console's language. A planned text is in English, and on a console in
// another language the game's own text stays.
constexpr std::string_view TextPacks[] = { "Mals/USen.Product.110.sarc.zs", "Mals/EUen.Product.110.sarc.zs" };
constexpr std::string_view StoneMessages = "EventFlowMsg/GossipStone.msbt";

// Every stone's talk runs this flow. Without the Mask of Truth held and worn, Navi talks in the stone's place. With it,
// the stone's talk opens, the stone says its lines about the mask, and then its message.
constexpr std::string_view StoneFlow = "Event/EventFlow/GossipStone.bfevfl.zs";
constexpr std::string_view MaskHeldQuery = "Event112";
constexpr std::string_view StoneTalkOpening = "Event6";
constexpr std::string_view StoneMessage = "Event18";
// The mask question goes on to the talk's opening on both answers, and the opening straight on to the message.
constexpr std::pair<std::string_view, std::string_view> StoneFlowNext[] = { { MaskHeldQuery, StoneTalkOpening },
                                                                            { StoneTalkOpening, StoneMessage } };

// A new game runs this flow. The loading entry point plays the narrated opening, then asks whether Kokiri Forest has
// loaded. The introduction waits, plays Navi waking Link, and fades out before it shows him in his house. The Deku
// Tree's quest and the forest's first tutorial wait for Navi's scene to count as played, which a scene left out never
// does.
constexpr std::string_view OpeningFlow = "Event/EventFlow/OpeningEvent.bfevfl.zs";
constexpr std::string_view LoadingEntry = "Opening_LoadingKokiriForest";
constexpr std::string_view ForestLoadedQuery = "Event18";
constexpr std::string_view WaitBeforeNavi = "Event27";
constexpr std::string_view FadeOutAfterNavi = "Event25";
constexpr std::string_view NaviWokeLink = "HavePlayedEvent.Dm_Kokiri_0030";

// The Kokiri Sword chest's event, entry point GetSword of this flow, gives what the chest holds in Event16, plays the
// scene of Link raising the sword, and from Event152 picks Navi's line. The scene puts a Kokiri Sword in his weapon
// slot that is not in his pouch, and Mido lets him by with it.
constexpr uint64_t KokiriSwordChest = 16757992849254467386u;
constexpr std::string_view KokiriSword = "Item_Sword_Kokiri";
constexpr std::string_view SwordFlow = "Event/EventFlow/Get_StoneOfForest.bfevfl.zs";
constexpr std::string_view SwordChestGive = "Event16";
constexpr std::string_view NaviLineAfterSword = "Event152";

// The text pack with each planned stone saying its text, and each planned message its text after the codes the game's
// message opens with.
static std::string TextPack(GameFile& game, const std::string& path, const std::string& dictionary, const Plan& plan) {
    try {
        sarc::Files pack = sarc::Parse(Decompress(game.Read(path), dictionary));
        std::map<std::string, std::vector<msbt::Message>> files;
        const auto message = [&](const std::string& file, const std::string& label) -> msbt::Message& {
            auto messages = files.find(file);
            if (messages == files.end()) {
                const auto data = pack.find(file);
                if (data == pack.end()) {
                    throw std::runtime_error(std::format("the pack has no {}", file));
                }
                messages = files.emplace(file, msbt::Parse(data->second)).first;
            }
            const auto found = std::ranges::find(messages->second, label, &msbt::Message::label);
            if (found == messages->second.end()) {
                throw std::runtime_error(std::format("{} has no message with the label {}", file, label));
            }
            return *found;
        };
        for (const PlannedStoneText& stone : plan.stoneTexts) {
            message(std::string(StoneMessages), stone.label).text = stone.text;
        }
        for (const PlannedMessageText& planned : plan.messageTexts) {
            msbt::Message& text = message(planned.file, planned.label);
            text.text = std::string(msbt::Opening(text.text)) + planned.text;
        }
        for (const auto& [file, messages] : files) {
            pack[file] = msbt::Write(messages);
        }
        return sarc::Write(pack, sarc::TextPackAlignment);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("{}: {}", path, e.what()));
    }
}

std::map<std::string, std::string> ModFiles(const std::filesystem::path& nup, const Plan& plan) {
    GameFile game(nup);
    // With a program of another build the game would run without the program patch.
    if (game.ProgramBuildId() != PatchedBuildId) {
        throw std::runtime_error(
            "This game file is another version of the game. Kaepora works only with version 2.0.0.");
    }
    const std::string dictionary = Dictionary(game, "bcett.byml.zsdic");

    std::map<std::string, byml::Document> placements;
    const auto placement = [&](const std::string& path) -> byml::Node& {
        auto found = placements.find(path);
        if (found == placements.end()) {
            found = placements.emplace(path, byml::Parse(Decompress(game.Read(path), dictionary))).first;
        }
        return found->second.root;
    };
    std::set<std::string> changed;
    for (const PlannedChest& chest : plan.chests) {
        for (const std::string& path : chest.files) {
            try {
                if (SetItem(Actor(placement(path), chest.hash), chest.dropActor)) {
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("chest {} in {}: {}", chest.hash, path, e.what()));
            }
        }
    }

    try {
        MoveSheikWaitBehindSword(placement(std::string(TempleStatic)));
        changed.insert(std::string(TempleStatic));
    } catch (const std::exception& e) { throw std::runtime_error(std::format("{}: {}", TempleStatic, e.what())); }

    const std::string packDictionary = Dictionary(game, "pack.zsdic");
    std::map<std::string, ShopPack> packs;
    std::set<std::string> soldOnce;
    ActorTables tables;
    for (const std::string_view path : ActorTablePaths) {
        tables.emplace(path, byml::Parse(Decompress(game.Read(std::string(path)))));
    }
    BringContainers(tables.find(PouchActorInfoPath)->second.root);
    std::map<std::string, NewActor> goodsActors;
    std::map<std::string, NewActor> tokenCopies;
    std::map<std::string, NewActor> pickupCopies;
    for (const PlannedShopSlot& slot : plan.shops) {
        try {
            std::string goodsActor;
            int32_t priceOffset = slot.price;
            const ShopGoods& goods = Goods(slot.item);
            const GoodsModel* model = std::get_if<GoodsModel>(&goods);
            if (model == nullptr) {
                goodsActor = std::get<std::string>(goods);
                priceOffset -= BasePrice(tables.find(PouchActorInfoPath)->second.root, goodsActor);
            } else {
                auto actor = goodsActors.find(slot.item);
                if (actor == goodsActors.end()) {
                    actor = goodsActors.emplace(slot.item, GoodsActor(game, packDictionary, tables, slot.item)).first;
                }
                goodsActor = actor->second.name;
            }
            const uint32_t quantity = Quantity(Row(tables.find(PouchActorInfoPath)->second.root, slot.item));
            for (const Shopkeeper& keeper : slot.shopkeepers) {
                const std::string path = std::format("Pack/Actor/{}.pack.zs", keeper.actor);
                auto pack = packs.find(path);
                if (pack == packs.end()) {
                    pack = packs.emplace(path, ShopPack{ sarc::Parse(Decompress(game.Read(path), packDictionary)) }).first;
                }
                SetGoods(pack->second.GoodsList(keeper.goodsList), slot.slot, goodsActor, priceOffset);
                soldOnce.insert(std::format("Shop_SoldNum.{}.{}", keeper.actor, slot.slot));
            }
            for (const PlacedActor& shelf : slot.shelves) {
                try {
                    byml::Node& shelfActor = Actor(placement(shelf.file), shelf.hash);
                    if (SetGyaml(shelfActor, goodsActor)) {
                        changed.insert(shelf.file);
                    }
                    if (model != nullptr) {
                        StandOnShelf(shelfActor, *model);
                        changed.insert(shelf.file);
                    }
                    if (ShowQuantity(game, placement(shelf.file), shelf.hash, quantity)) {
                        changed.insert(shelf.file);
                    }
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::format("shelf {} in {}: {}", shelf.hash, shelf.file, e.what()));
                }
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("shop slot {} of {}: {}", slot.slot, slot.shopkeepers.front().actor, e.what()));
        }
    }

    for (const PlannedActorItem& skulltula : plan.skulltulas) {
        if (skulltula.item == TokenItem) {
            continue;
        }
        auto copy = tokenCopies.find(skulltula.item);
        if (copy == tokenCopies.end()) {
            try {
                copy = tokenCopies.emplace(skulltula.item, TokenCopy(game, packDictionary, tables, skulltula.item))
                           .first;
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("skulltula {}: {}", skulltula.hash, e.what()));
            }
        }
        for (const std::string& path : skulltula.files) {
            try {
                if (SetDrop(Actor(placement(path), skulltula.hash), copy->second.name)) {
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("skulltula {} in {}: {}", skulltula.hash, path, e.what()));
            }
        }
    }
    for (const PlannedActorItem& loose : plan.looseItems) {
        std::string pickup = loose.item;
        if (std::ranges::find(LoosePickups, loose.item) == LoosePickups.end()) {
            auto copy = pickupCopies.find(loose.item);
            if (copy == pickupCopies.end()) {
                try {
                    copy = pickupCopies.emplace(loose.item, PickupCopy(game, packDictionary, tables, loose.item)).first;
                } catch (const std::exception& e) {
                    throw std::runtime_error(std::format("loose item {}: {}", loose.hash, e.what()));
                }
            }
            pickup = copy->second.name;
        }
        for (const std::string& path : loose.files) {
            try {
                if (SetGyaml(Actor(placement(path), loose.hash), pickup)) {
                    changed.insert(path);
                }
            } catch (const std::exception& e) {
                throw std::runtime_error(std::format("loose item {} in {}: {}", loose.hash, path, e.what()));
            }
        }
    }

    const std::string zsDictionary = Dictionary(game, "zs.zsdic");
    if (!plan.presences.empty()) {
        const std::set<uint32_t> flags = PresenceFlags(game, zsDictionary);
        for (const PlannedPresence& presence : plan.presences) {
            try {
                if (!flags.contains(Murmur3(presence.flag))) {
                    throw std::runtime_error(std::format(
                        "\"{}\" is not a true-or-false value or expression in the game data list", presence.flag));
                }
                byml::Node& actor = Actor(placement(presence.actor.file), presence.actor.hash);
                if (Find(actor, "Presence") != nullptr) {
                    throw std::runtime_error("the actor already has a presence");
                }
                // The game's presences that wait for a quest step take this shape.
                FindOrAdd(actor, "Presence", byml::Type::Dict).dict = {
                    { .key = "FlagName", .value = { .type = byml::Type::String, .string = presence.flag } },
                    { .key = "IsCheckLazy", .value = { .type = byml::Type::Bool } },
                    { .key = "IsNegation", .value = { .type = byml::Type::Bool } },
                };
                changed.insert(presence.actor.file);
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    std::format("presence {} in {}: {}", presence.actor.hash, presence.actor.file, e.what()));
            }
        }
    }
    std::map<std::string, bfevfl::Flow> flows;
    std::set<std::string> changedFlows;
    const auto flowAt = [&](const std::string& path) -> bfevfl::Flow& {
        auto flow = flows.find(path);
        if (flow == flows.end()) {
            flow = flows.emplace(path, bfevfl::Parse(Decompress(game.Read(path), zsDictionary))).first;
        }
        return flow->second;
    };
    for (const PlannedEventText& text : plan.eventTexts) {
        try {
            if (bfevfl::SetString(flowAt(text.flow), text.event, text.param, text.text)) {
                changedFlows.insert(text.flow);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("event text {}: {}", Name(text.flow, text.event, text.param), e.what()));
        }
    }
    for (const PlannedEventNumber& number : plan.eventNumbers) {
        try {
            if (bfevfl::SetInt(flowAt(number.flow), number.event, number.param, number.value)) {
                changedFlows.insert(number.flow);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::format("event number {}: {}", Name(number.flow, number.event, number.param), e.what()));
        }
    }
    if (!plan.stoneTexts.empty()) {
        const std::string path(StoneFlow);
        for (const auto& [event, next] : StoneFlowNext) {
            if (bfevfl::SetNext(flowAt(path), event, next)) {
                changedFlows.insert(path);
            }
        }
    }
    if (plan.openingSkipped) {
        const std::string path(OpeningFlow);
        if (bfevfl::SetEntry(flowAt(path), LoadingEntry, ForestLoadedQuery)) {
            changedFlows.insert(path);
        }
        if (bfevfl::SetNext(flowAt(path), WaitBeforeNavi, FadeOutAfterNavi)) {
            changedFlows.insert(path);
        }
    }
    if (std::ranges::any_of(plan.chests, [](const PlannedChest& chest) {
            return chest.hash == KokiriSwordChest && chest.dropActor != KokiriSword;
        })) {
        const std::string path(SwordFlow);
        if (bfevfl::SetNext(flowAt(path), SwordChestGive, NaviLineAfterSword)) {
            changedFlows.insert(path);
        }
    }

    std::map<std::string, std::string> mod{ { std::format("exefs/{}.ips", PatchedBuildId),
                                              ProgramPatch(plan.cutscenesSkipped) } };
    const auto addToRomfs = [&](std::string_view path, std::string data) {
        mod.emplace(std::format("romfs/{}", path), std::move(data));
    };
    for (const std::string_view path : BombLiftPacks) {
        addToRomfs(path, BombLiftPack(game, packDictionary, path));
    }
    if (plan.midoCastleEntryRemoved) {
        addToRomfs(MidoSchedule, MidoScheduleWithoutCastleEntry(game));
    }
    addToRomfs(PedestalModule, OpenPedestal(game));
    std::string table = ReadSizeTable(game);
    bool tableChanged = false;
    for (const std::string& path : changed) {
        const std::string data = byml::Write(placements.at(path));
        tableChanged |= RaiseSize(table, path, data.size(), PlacementOverhead);
        addToRomfs(path, Compress(data, dictionary));
    }
    for (const std::string& path : changedFlows) {
        const std::string data = bfevfl::Write(flows.at(path));
        tableChanged |= RaiseSize(table, path, data.size(), EventFlowOverhead);
        addToRomfs(path, Compress(data, zsDictionary));
    }
    if (!plan.stoneTexts.empty() || !plan.messageTexts.empty()) {
        for (const std::string_view path : TextPacks) {
            const std::string data = TextPack(game, std::string(path), zsDictionary, plan);
            tableChanged |= RaiseSize(table, path, data.size(), TextPackOverhead);
            addToRomfs(path, Compress(data, zsDictionary));
        }
    }
    for (auto& [path, pack] : packs) {
        for (const auto& [name, list] : pack.goodsLists) {
            pack.files[name] = byml::Write(list);
        }
        const std::string data = sarc::Write(pack.files, sarc::ActorPackAlignment);
        tableChanged |= RaiseSize(table, path, data.size(), ActorPackOverhead);
        addToRomfs(path, Compress(data, packDictionary));
    }
    // Each new actor's pack.
    std::vector<const NewActor*> newActors;
    for (const auto* actors : { &goodsActors, &tokenCopies, &pickupCopies }) {
        for (const NewActor& actor : std::views::values(*actors)) {
            newActors.push_back(&actor);
        }
    }
    for (const NewActor* actor : newActors) {
        const std::string path = std::format("Pack/Actor/{}.pack.zs", actor->name);
        const std::string data = sarc::Write(actor->pack, sarc::ActorPackAlignment);
        AddSize(table, path, data.size(), ActorPackOverhead);
        tableChanged = true;
        addToRomfs(path, Compress(data, packDictionary));
    }
    for (auto& [path, rows] : tables) {
        if (newActors.empty() && path != PouchActorInfoPath) {
            continue;
        }
        for (const NewActor* actor : newActors) {
            AddRow(rows.root, actor->rows.find(path)->second);
        }
        const std::string data = byml::Write(rows);
        const size_t overhead = path == ActorInfoPath ? ActorInfoOverhead : ActorTableOverhead;
        tableChanged |= RaiseSize(table, path, data.size(), overhead);
        addToRomfs(path, Compress(data));
    }
    if (tableChanged) {
        addToRomfs(SizeTablePath, Compress(table));
    }
    std::map<std::string, StartValue> startValues = plan.startValues;
    if (plan.openingSkipped && !startValues.emplace(NaviWokeLink, true).second) {
        throw std::runtime_error(std::format("the plan's start values already name {}", NaviWokeLink));
    }
    for (const std::string& item : plan.startItems) {
        try {
            for (auto& [name, value] : StartItemValues(Row(tables.find(PouchActorInfoPath)->second.root, item), item)) {
                if (!startValues.emplace(name, value).second) {
                    throw std::runtime_error(std::format("the plan's start values already name {}", name));
                }
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("start item {}: {}", item, e.what()));
        }
    }
    if (!startValues.empty() || !soldOnce.empty()) {
        addToRomfs(GameDataListPath, GameDataList(game, zsDictionary, startValues, soldOnce));
    }
    return mod;
}

void WriteFile(const std::filesystem::path& path, std::string_view data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), std::streamsize(data.size()));
    file.close();
    if (!file) {
        throw std::runtime_error(std::format("{}: cannot write", path.string()));
    }
}
