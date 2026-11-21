"""Questions to the logic, answered by kaepora-reach.

Usage: python3 -I -B tests/logic_test.py <kaepora-reach> [test name ...]
Each test places items at locations, leaves every other location as the unmodified game has it, and asks whether
Link can ever collect what is at one location. Names are SoH's English names.
"""

import pathlib
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import reach  # noqa: E402

# The 10 and 30 Gold Skulltula rewards are the only wallet upgrades.
NO_WALLET_UPGRADE = (("Kak 10 Gold Skulltula Reward", "Green Rupee"), ("Kak 30 Gold Skulltula Reward", "Green Rupee"))
ONE_WALLET_UPGRADE = ("Kak 30 Gold Skulltula Reward", "Green Rupee")
NO_HAMMER = ("Fire Temple Megaton Hammer Chest", "Green Rupee")
NO_SILVER_GAUNTLETS = ("Spirit Temple Silver Gauntlets Chest", "Green Rupee")
NO_GOLDEN_GAUNTLETS = ("Ganon's Castle Shadow Trial Golden Gauntlets Chest", "Green Rupee")
SILVER_GAUNTLETS_AT_START = ("Link's Pocket", "Silver Gauntlets")
# Without the Ocarina of Time, Link never becomes adult.
NEVER_ADULT = ("HF Ocarina of Time Item", "Green Rupee")
# Dampe's race gives the first hookshot. The Water Temple's Longshot chest lies past places that need one.
NO_HOOKSHOT = ("Graveyard Hookshot Chest", "Green Rupee")
# The well holds the only Lens of Truth.
NO_LENS = ("Bottom of the Well Lens of Truth Chest", "Green Rupee")
# Only Dodongo's Cavern holds a bomb bag, and bombchus count only with a bag, so without it Link holds no explosive.
# King Dodongo falls only to explosives, so Link never becomes adult either.
NO_BOMB_BAG = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"),)
# The Boomerang, the Hookshot and the Longshot each fetch a Gold Skulltula's token from afar.
NO_BOOMERANG = ("Jabu Jabus Belly Boomerang Chest", "Green Rupee")
NO_RANGED = (NO_BOOMERANG, NO_HOOKSHOT, ("Water Temple Longshot Chest", "Green Rupee"))
# The Deku Tree's chest and the market's gallery give the slingshot, and the Forest Temple's chest gives the bow.
NO_SLINGSHOT_OR_BOW = (("Deku Tree Slingshot Chest", "Green Rupee"), ("Market Shooting Gallery", "Green Rupee"),
                       ("Forest Temple Bow Chest", "Green Rupee"))
NO_BOTTLE = (("LH Underwater Item", "Green Rupee"), ("Market 10 Big Poes", "Green Rupee"),
             ("Kak Anju as Child", "Green Rupee"), ("LLR Talons Chickens", "Green Rupee"))
LENS_NEVER_NEEDED = {"Lens of Truth": "Never needed"}
KEYS_NOT_NEEDED = {"Small and boss keys": "Not needed"}
FOREST_OPEN = {"Kokiri Forest": "Open"}
CLOSED_DEKU = {"Kokiri Forest": "Closed Deku"}
BRIDGE_OPEN = {"Rainbow Bridge": "Always open"}
NO_TRIALS = {"Ganon's trials": "None"}


class LogicTest(unittest.TestCase):
    reachable = staticmethod(reach.reachable)

    def test_placed_items_change_the_answer(self):
        self.assertTrue(self.reachable("Queen Gohma"))
        self.assertFalse(self.reachable("Queen Gohma", ("KF Kokiri Sword Chest", "Green Rupee")))

    def test_links_pocket_takes_one_item(self):
        result = subprocess.run([reach.PROGRAM, "{}", "Queen Gohma", "Link's Pocket", "Silver Gauntlets",
                                 "Link's Pocket", "Green Rupee"], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout), (2, ""), result.stderr)
        self.assertIn("Link's Pocket", result.stderr)

    # A Spiritual Stone or a medallion opens nothing in the remake. A boss's blue warp completes its dungeon's quest,
    # whatever its place gives, and the game's doors and scenes wait for those quests. So a boss's place that gives
    # something else opens all that its reward opened, and a reward found elsewhere opens nothing.

    def test_the_ocarina_of_time_and_the_door_of_time_follow_king_dodongo_and_barinade(self):
        # Zelda flees and drops the Ocarina of Time in the moat once the Fire and the Water quests are complete. Only
        # Dodongo's Cavern holds a bomb bag, and King Dodongo falls only to explosives.
        no_stones = (("King Dodongo", "Green Rupee"), ("Barinade", "Green Rupee"))
        self.assertTrue(self.reachable("HF Ocarina of Time Item", *no_stones))
        self.assertTrue(self.reachable("Gift from Rauru", *no_stones))
        self.assertFalse(self.reachable("HF Ocarina of Time Item", *no_stones,
                                        ("KF Mido Top Left Chest", "Goron's Ruby"),
                                        ("KF Mido Top Right Chest", "Zora's Sapphire"),
                                        ("Dodongos Cavern Bomb Bag Chest", "Green Rupee")))
        # Mido keeps a child without the Kokiri Sword from the Deku Tree, and the forest's chest holds the only one.
        # Dodongo's Cavern's eye switches want the slingshot, and the first one lies in the Deku Tree.
        never_gohma = (("KF Kokiri Sword Chest", "Green Rupee"), ("KF Mido Top Left Chest", "Progressive Slingshot"))
        self.assertFalse(self.reachable("Queen Gohma", *never_gohma, settings=CLOSED_DEKU))
        for location in ("HF Ocarina of Time Item", "Song from Ocarina of Time", "Gift from Rauru"):
            self.assertTrue(self.reachable(location, *never_gohma, settings=CLOSED_DEKU), location)

    def test_the_prelude_of_light_follows_phantom_ganon(self):
        # Sheik waits at the pedestal once the Forest quest is complete. The Forest Temple's only bow lies on the way to
        # Phantom Ganon.
        self.assertTrue(self.reachable("Sheik at Temple", ("Phantom Ganon", "Green Rupee")))
        self.assertFalse(self.reachable("Sheik at Temple", ("Phantom Ganon", "Green Rupee"),
                                        ("KF Mido Top Left Chest", "Forest Medallion"),
                                        ("Forest Temple Bow Chest", "Green Rupee")))

    def test_the_nocturne_of_shadow_follows_phantom_ganon_volvagia_and_morpha(self):
        # Kakariko burns once the Forest, the Fire and the Water quests are complete.
        no_medallions = (("Phantom Ganon", "Green Rupee"), ("Volvagia", "Green Rupee"), ("Morpha", "Green Rupee"))
        self.assertTrue(self.reachable("Sheik in Kakariko", *no_medallions))
        self.assertFalse(self.reachable("Sheik in Kakariko", *no_medallions,
                                        ("KF Mido Top Left Chest", "Forest Medallion"),
                                        ("KF Mido Top Right Chest", "Fire Medallion"),
                                        ("KF Mido Bottom Left Chest", "Water Medallion"),
                                        ("Forest Temple Bow Chest", "Green Rupee")))

    def test_the_light_arrows_and_the_bridge_follow_bongo_bongo_and_twinrova(self):
        # Zelda waits once the Shadow and the Spirit sages have woken, in the scenes after Bongo Bongo and Twinrova, and
        # the bridge appears after her. A Spirit Temple key at the start lets the search reach Twinrova at all: SoH
        # counts the temple's keys for the worst order a player can spend them in. The well holds the Lens the Shadow
        # Temple takes.
        no_medallions = (("Bongo Bongo", "Green Rupee"), ("Twinrova", "Green Rupee"),
                         ("Link's Pocket", "Spirit Temple Small Key"))
        self.assertTrue(self.reachable("Market ToT Light Arrow Cutscene", *no_medallions))
        self.assertTrue(self.reachable("Ganon's Castle Forest Trial Chest", *no_medallions))
        self.assertFalse(self.reachable("Market ToT Light Arrow Cutscene", *no_medallions, NO_LENS,
                                        ("KF Mido Top Left Chest", "Shadow Medallion"),
                                        ("KF Mido Top Right Chest", "Spirit Medallion")))

    def test_child_comes_back_before_the_forest_temple(self):
        # Only child Link can drain the well, with the Song of Storms that only adult Link learns. With the Fairy Bow in
        # the well, adult Link cannot finish the Forest Temple, and he still puts the Master Sword back.
        self.assertTrue(self.reachable("Bottom of the Well Lens of Truth Chest",
                                       ("Bottom of the Well Lens of Truth Chest", "Progressive Bow"),
                                       ("Forest Temple Bow Chest", "Green Rupee")))

    def test_a_child_warps_where_he_cannot_walk_out_only_with_a_song_that_takes_him_out(self):
        # A child cannot walk out of the Desert Colossus, where only the Requiem takes him, nor get across to an exit
        # from the middle of Death Mountain Crater, where the Bolero lands him and he plants the bean of its skulltula.
        # The Minuet, the Serenade, the Nocturne and the Prelude land him where he can walk out.
        no_way_out = (("Sheik in Forest", "Green Rupee"), ("Sheik in Ice Cavern", "Green Rupee"),
                      ("Sheik in Kakariko", "Green Rupee"), ("Sheik at Temple", "Green Rupee"))
        for location in ("Spirit Temple Child Early Torches Chest", "DMC GS Bean Patch"):
            self.assertFalse(self.reachable(location, *no_way_out), location)
            for song in no_way_out:
                others = [pair for pair in no_way_out if pair != song]
                self.assertTrue(self.reachable(location, *others), (location, song[0]))

    def test_only_adult_link_buys_a_tunic(self):
        never_adult = ("HF Ocarina of Time Item", "Green Rupee")
        self.assertFalse(self.reachable("ZD Shop Item 1", never_adult))
        self.assertFalse(self.reachable("GC Shop Item 5", never_adult))

    def test_a_tunic_needs_the_largest_wallet(self):
        # Each tunic costs 600.
        for location in ("ZD Shop Item 1", "GC Shop Item 5"):
            self.assertFalse(self.reachable(location, ONE_WALLET_UPGRADE))
            self.assertTrue(self.reachable(location))
        # The price belongs to the shop slot, so it holds whatever is placed there.
        self.assertFalse(self.reachable("GC Shop Item 5", ONE_WALLET_UPGRADE, ("GC Shop Item 5", "Buy Goron Tunic")))

    def test_a_potion_shop_is_never_the_first_blue_fire(self):
        self.assertTrue(self.reachable("Market Potion Shop Item 2"))
        # Without the Ocarina of Time, Link never becomes adult, and only adult Link reaches other Blue Fire.
        self.assertFalse(self.reachable("Market Potion Shop Item 2", ("HF Ocarina of Time Item", "Green Rupee"),
                                        ("KF Mido Top Left Chest", "Empty Bottle")))

    def test_shops_charge_the_remakes_prices(self):
        # A fish costs 50 at the Market Potion Shop. 20 bombchus cost 240.
        self.assertTrue(self.reachable("Market Potion Shop Item 8", *NO_WALLET_UPGRADE))
        self.assertFalse(self.reachable("Market Bombchu Shop Item 5", *NO_WALLET_UPGRADE))

    def test_wallets_hold_200_then_500(self):
        # A poe costs 200 at the Market Potion Shop. 15 bombchus cost 210 and 30 cost 300.
        self.assertTrue(self.reachable("Market Potion Shop Item 7", *NO_WALLET_UPGRADE))
        self.assertFalse(self.reachable("Market Bombchu Shop Item 7", *NO_WALLET_UPGRADE))
        self.assertTrue(self.reachable("Market Bombchu Shop Item 8", ONE_WALLET_UPGRADE))

    def test_epona_comes_only_after_malons_lesson(self):
        # Horseback archery needs Epona. Ingo races adult Link for her only once child Malon's lesson has played,
        # whatever song it teaches.
        self.assertTrue(self.reachable("GF HBA 1000 Points", ("Song from Malon", "Saria's Song"),
                                       ("Song from Saria", "Epona's Song")))
        # Without the Weird Egg, Talon never wakes and Malon never goes home to teach. Zelda's Letter from the start
        # opens the village gate, and the Lullaby and Saria's Song in the forest win the stones, so Link still grows
        # up and holds Epona's Song.
        no_lesson = (("HC Malon Egg", "Green Rupee"), ("Link's Pocket", "Zelda's Letter"),
                     ("KF Mido Top Left Chest", "Zelda's Lullaby"), ("KF Mido Top Right Chest", "Saria's Song"),
                     ("KF Mido Bottom Left Chest", "Epona's Song"))
        self.assertTrue(self.reachable("Song from Windmill", *no_lesson))
        self.assertFalse(self.reachable("GF HBA 1000 Points", *no_lesson))

    def test_an_early_requiem_waits_for_the_fortress_at_the_spirit_temple(self):
        # With the Requiem learned as a child, Link warps to the Desert Colossus before the Gerudo Fortress. The
        # Colossus is open to him, but the Spirit Temple's front entrance waits for an adult who has entered the
        # Wasteland from the fortress, through the gate the freed carpenters' card opens. Malon's Serenade takes him
        # out again.
        early = (("Song from Impa", "Requiem of Spirit"), ("Sheik at Colossus", "Zelda's Lullaby"),
                 ("Song from Malon", "Serenade of Water"), ("Sheik in Ice Cavern", "Epona's Song"))
        no_card = ("GF Freed All Carpenters", "Green Rupee")
        for location in ("Sheik at Colossus", "Colossus Great Fairy Reward", "Colossus GS Tree",
                         "Colossus Grotto Chest"):
            self.assertTrue(self.reachable(location, *early, no_card, SILVER_GAUNTLETS_AT_START), location)
        self.assertFalse(self.reachable("Spirit Temple Child Bridge Chest", *early, no_card))
        self.assertTrue(self.reachable("Spirit Temple Child Bridge Chest", *early))

    def test_an_adult_warps_to_the_crater_only_where_he_gets_across_or_with_a_song_that_takes_him_out(self):
        # An adult gets across from the crater's warp pad as the pad's own way to the pots asks: with the Hookshot,
        # the Hover Boots or a planted bean. Here the Bolero is his only warp song, and he has no Hookshot.
        def only_the_bolero(ice_cavern):
            return (("Song from Malon", "Bolero of Fire"), ("Sheik in Crater", "Epona's Song"),
                    ("Sheik in Forest", "Green Rupee"), ("Sheik in Ice Cavern", ice_cavern),
                    ("Sheik in Kakariko", "Green Rupee"), ("Sheik at Colossus", "Green Rupee"),
                    ("Sheik at Temple", "Song of Storms"), NO_HOOKSHOT)
        self.assertFalse(self.reachable("Fire Temple Near Boss Chest", *only_the_bolero("Green Rupee")))
        self.assertTrue(self.reachable("Fire Temple Near Boss Chest", *only_the_bolero("Serenade of Water")))

    def test_an_adult_warps_to_the_colossus_only_with_a_song_that_takes_him_out(self):
        # The Colossus's way into the Wasteland stands for an adult only once he has walked into the Wasteland from the
        # fortress, a walk no route names, so he can no more walk out of the Colossus than a child. Here the Requiem is
        # his only warp song, and Epona takes him across the valley to the fortress. Without the card he cannot walk to
        # the Colossus either, and without the Hover Boots and the Longshot he cannot cross the sand. Sheik at the
        # temple teaches the Song of Storms, so the child still comes back for the well's Lens.
        def only_the_requiem(crater):
            return (("Song from Malon", "Requiem of Spirit"), ("Sheik in Forest", "Epona's Song"),
                    ("Sheik at Colossus", "Green Rupee"), ("Sheik in Crater", crater),
                    ("Sheik in Ice Cavern", "Green Rupee"), ("Sheik in Kakariko", "Green Rupee"),
                    ("Sheik at Temple", "Song of Storms"), SILVER_GAUNTLETS_AT_START)
        no_card = ("GF Freed All Carpenters", "Green Rupee")
        no_crossing = (("Shadow Temple Hover Boots Chest", "Green Rupee"), ("Water Temple Longshot Chest", "Green Rupee"))
        self.assertFalse(self.reachable("Colossus Grotto Chest", *only_the_requiem("Green Rupee"), no_card))
        self.assertTrue(self.reachable("GF Freed All Carpenters", *only_the_requiem("Green Rupee"), *no_crossing))
        self.assertFalse(self.reachable("Colossus Grotto Chest", *only_the_requiem("Green Rupee"), *no_crossing))
        self.assertTrue(self.reachable("Colossus Grotto Chest", *only_the_requiem("Green Rupee")))
        self.assertTrue(self.reachable("Colossus Grotto Chest", *only_the_requiem("Prelude of Light"), *no_crossing))

    def test_crater_great_fairy_needs_magic(self):
        self.assertFalse(self.reachable("DMC Great Fairy Reward", ("DMT Great Fairy Reward", "Green Rupee")))

    def test_bombchu_shop_opens_after_dodongos_cavern(self):
        # Without Goron's Bracelet, child Link cannot open Dodongo's Cavern, so he never beats King Dodongo and never
        # becomes adult.
        self.assertFalse(self.reachable("Market Bombchu Shop Item 1", ("GC Darunias Joy", "Green Rupee")))

    def test_dodongos_cavern_opens_to_the_bomb_flower_above_it(self):
        # Bombs from the bag do not break the rock at the mouth. The fruit of the bomb flower on the ledge above does,
        # and picking the flower takes the bracelet's strength.
        child_with_bombs_without_bracelet = (NEVER_ADULT, ("GC Darunias Joy", "Green Rupee"),
                                             ("KF Mido Top Left Chest", "Progressive Bomb Bag"))
        self.assertFalse(self.reachable("Dodongos Cavern Map Chest", *child_with_bombs_without_bracelet))

    def test_biggorons_sword_needs_the_whole_trade_chain(self):
        self.assertTrue(self.reachable("DMT Trade Claim Check"))
        self.assertFalse(self.reachable("DMT Trade Claim Check", ("LH Lab Trade Eyeball Frog", "Green Rupee")))

    def test_dampes_heart_piece_needs_a_hookshot(self):
        self.assertFalse(self.reachable("Graveyard Dampe Race Freestanding PoH",
                                        ("Graveyard Hookshot Chest", "Green Rupee")))

    def test_deku_tree_slingshot_room_side_chest_needs_an_eye_hit(self):
        # An eye in the room drops the ladder to the side chest. Without the slingshot child Link reaches only the
        # room's other chest.
        self.assertTrue(self.reachable("Deku Tree Slingshot Room Side Chest"))
        no_slingshot = ("Deku Tree Slingshot Chest", "Green Rupee")
        self.assertFalse(self.reachable("Deku Tree Slingshot Room Side Chest", no_slingshot))

    # Goron's Bracelet has a slot of its own. The Silver and Golden Gauntlets share another, which keeps the higher
    # tier found, and only adult Link can use them.

    def test_gauntlets_are_not_the_bracelet_for_a_child(self):
        # Without the Ocarina of Time, Link never becomes adult. Without the bracelet, a child cannot pick the bomb
        # flower whose fruit opens Dodongo's Cavern.
        child_without_bracelet = (("HF Ocarina of Time Item", "Green Rupee"), ("GC Darunias Joy", "Green Rupee"))
        self.assertFalse(self.reachable("Dodongos Cavern Map Chest", *child_without_bracelet,
                                        ("KF Mido Top Left Chest", "Silver Gauntlets"),
                                        ("KF Mido Top Right Chest", "Golden Gauntlets")))
        self.assertTrue(self.reachable("Dodongos Cavern Map Chest", *child_without_bracelet,
                                       ("KF Mido Top Left Chest", "Goron's Bracelet")))

    def test_a_bomb_needs_no_strength(self):
        # Every mod gives a bomb the bombchu's lift grade, None, so Link takes one out of the bag with no strength. The
        # rock before the Death Mountain Trail chest takes a bomb, a bombchu or the hammer, and the logic counts
        # bombchus only with a bomb bag.
        no_bracelet = ("GC Darunias Joy", "Green Rupee")
        bomb_bag = ("KF Mido Top Left Chest", "Progressive Bomb Bag")
        self.assertFalse(self.reachable("DMT Chest", NEVER_ADULT, no_bracelet))
        self.assertTrue(self.reachable("DMT Chest", NEVER_ADULT, no_bracelet, bomb_bag))

    def test_golden_gauntlets_lift_what_silver_gauntlets_lift(self):
        # Boulders only the Silver Gauntlets lift ring the rightmost chest of the Goron City maze.
        self.assertTrue(self.reachable("GC Maze Right Chest", NO_SILVER_GAUNTLETS,
                                       ("KF Mido Top Left Chest", "Golden Gauntlets")))

    def test_silver_gauntlets_do_not_lift_what_golden_gauntlets_lift(self):
        # Only the Golden Gauntlets clear the way to the Great Fairy fountain outside Ganon's Castle.
        self.assertFalse(self.reachable("OGC Great Fairy Reward", NO_GOLDEN_GAUNTLETS,
                                        ("KF Mido Top Left Chest", "Silver Gauntlets")))
        self.assertTrue(self.reachable("OGC Great Fairy Reward", NO_GOLDEN_GAUNTLETS,
                                       ("KF Mido Top Left Chest", "Golden Gauntlets")))

    def test_goron_city_maze_right_chest_needs_silver_gauntlets(self):
        # Boulders only the Silver Gauntlets lift ring the chest. Explosives and the hammer do not open the ring.
        self.assertFalse(self.reachable("GC Maze Right Chest", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS))
        self.assertTrue(self.reachable("GC Maze Right Chest", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS,
                                       ("KF Mido Top Left Chest", "Silver Gauntlets")))

    def test_goron_city_maze_first_row_takes_a_bomb_or_the_hammer(self):
        # Tan boulders close the maze's first row, and only a bomb or the hammer breaks them. Past it, the hammer or the
        # Silver Gauntlets reach the left chest, and explosives alone do not.
        self.assertTrue(self.reachable("GC Maze Center Chest", NEVER_ADULT))
        self.assertFalse(self.reachable("GC Maze Center Chest", NEVER_ADULT, *NO_BOMB_BAG))
        self.assertTrue(self.reachable("GC Maze Left Chest", SILVER_GAUNTLETS_AT_START, NO_HAMMER))
        self.assertFalse(self.reachable("GC Maze Left Chest", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS, NO_HAMMER))

    def test_goron_city_maze_center_side_chest_is_reachable(self):
        self.assertTrue(self.reachable("GC Maze Center Side Chest"))

    def test_hidden_grottos_open_without_the_echo_gem(self):
        # The game opens a hidden grotto to a bomb or the hammer, or the Song of Storms, and never checks the Echo Gem
        # (SoH's Stone of Agony), which the House of Skulltula gives for 20 tokens.
        no_echo_gem = ("Kak 20 Gold Skulltula Reward", "Green Rupee")
        self.assertTrue(self.reachable("KF Storms Grotto Chest", no_echo_gem))

    def test_goron_city_grotto_chest_needs_the_hookshot(self):
        # Two hookshot targets cross the lava room to the grotto. Goron City has no Song of Time block.
        self.assertTrue(self.reachable("GC Grotto Chest"))
        self.assertFalse(self.reachable("GC Grotto Chest", NO_HOOKSHOT))

    def test_the_bracelet_lifts_nothing_that_needs_gauntlets(self):
        # Darunia gives the bracelet.
        self.assertFalse(self.reachable("OGC Great Fairy Reward", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS))

    def test_training_ground_door_behind_the_heavy_block_opens_to_the_hookshot(self):
        # Without the bow, the Forest Temple and so the well's Lens of Truth are out of reach, so the gauntlets and the
        # Lens come from elsewhere. The hookshot hits the eye above the barred door into the room of all four chests.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        outside = (("KF Mido Top Left Chest", "Silver Gauntlets"), ("KF Mido Top Right Chest", "Lens of Truth"))
        self.assertTrue(self.reachable("Gerudo Training Ground Heavy Block Second Chest", no_bow, *outside))

    def test_training_ground_door_behind_the_heavy_block_needs_no_lens(self):
        # An arrow from the floor in front of the barred door, behind the pushed block, hits the eye above it with the
        # Lens of Truth off. The well holds the only Lens. Without it the wasteland stays shut, so the gauntlets that push
        # the block come from elsewhere.
        no_lens = ("Bottom of the Well Lens of Truth Chest", "Green Rupee")
        self.assertTrue(self.reachable("Gerudo Training Ground Heavy Block First Chest", no_lens,
                                       ("KF Mido Top Left Chest", "Silver Gauntlets")))

    def test_training_ground_lobby_left_chest_needs_no_eye_hit(self):
        # The invisible left chest stands there from the start. The eye switch shows only the right one.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        self.assertTrue(self.reachable("Gerudo Training Ground Lobby Left Chest", no_bow))

    def test_jabu_jabu_map_chest_needs_no_boomerang(self):
        # The chest stands beyond the basement below the holes, and no tentacle stands in the way.
        self.assertTrue(self.reachable("Jabu Jabus Belly Map Chest", ("Jabu Jabus Belly Boomerang Chest", "Green Rupee")))

    def test_invisible_chests_and_illusion_walls_need_no_lens(self):
        # An invisible chest opens for Link who walks up to it, and Link walks through an illusion wall.
        no_lens = ("Bottom of the Well Lens of Truth Chest", "Green Rupee")
        self.assertTrue(self.reachable("Gerudo Training Ground Hidden Ceiling Chest", no_lens))
        self.assertTrue(self.reachable("Bottom of the Well Front Left Fake Wall Chest", no_lens))
        self.assertTrue(self.reachable("Shadow Temple Map Chest", no_lens))

    def test_well_locks_each_take_both_keys(self):
        # Both locked doors open off the middle, so either can take the first of the two keys.
        self.assertTrue(self.reachable("Bottom of the Well GS West Inner Room"))
        self.assertTrue(self.reachable("Bottom of the Well GS East Inner Room"))
        one_key = ("Bottom of the Well Right Bottom Fake Wall Chest", "Green Rupee")
        self.assertFalse(self.reachable("Bottom of the Well GS West Inner Room", one_key))
        self.assertFalse(self.reachable("Bottom of the Well GS East Inner Room", one_key))

    def test_well_east_inner_room_skulltula_needs_no_lens(self):
        for settings in ({"Lens of Truth": "Needed"}, LENS_NEVER_NEEDED):
            self.assertTrue(self.reachable("Bottom of the Well GS East Inner Room", NO_LENS, settings=settings))

    def test_lens_never_needed_waives_every_rule_that_asks_for_it(self):
        for location in ("Bottom of the Well Compass Chest", "Bongo Bongo"):
            self.assertFalse(self.reachable(location, NO_LENS), location)
            self.assertTrue(self.reachable(location, NO_LENS, settings=LENS_NEVER_NEEDED), location)

    def test_lens_never_needed_counts_on_nothing_from_the_chest_game(self):
        # The prize asks for the Lens, so with the Lens never needed the chest keeps the game's heart piece. A seed that
        # counted that heart toward Link's health would wait for the Lens to win it.
        self.assertTrue(self.reachable("Market Treasure Chest Game Reward"))
        self.assertFalse(self.reachable("Market Treasure Chest Game Reward", settings=LENS_NEVER_NEEDED))

    def test_well_hidden_pits_door_has_no_lock(self):
        self.assertTrue(self.reachable("Bottom of the Well Fire Keese Chest",
                                       ("Bottom of the Well Right Bottom Fake Wall Chest", "Green Rupee"),
                                       ("Bottom of the Well Freestanding Key", "Green Rupee")))

    def test_forest_temple_locks_form_one_chain(self):
        # Five locked doors stand one behind the other: after the first room, into the west hallway, at the top of the
        # block push rooms, into the Blue Poe room and into the frozen eye room. Each takes one key more than the last.
        def without(*chests):
            return [(f"Forest Temple {chest} Chest", "Green Rupee") for chest in chests]
        self.assertFalse(self.reachable("Forest Temple First Stalfos Chest", *without("First Room")))
        # The temple's own bow lies behind the third door. One found outside hits the eye switches before it.
        bow = ("KF Mido Top Left Chest", "Progressive Bow")
        self.assertFalse(self.reachable("Forest Temple Eye Switch Chest", bow, *without("First Stalfos", "Well")))
        self.assertFalse(self.reachable("Forest Temple Boss Key Chest", bow, *without("Well", "Floormaster")))
        self.assertTrue(self.reachable("Forest Temple Boss Key Chest", bow, *without("Floormaster", "Red Poe")))
        self.assertFalse(self.reachable("Forest Temple Blue Poe Chest", *without("Floormaster", "Red Poe")))
        self.assertTrue(self.reachable("Forest Temple Blue Poe Chest", *without("Floormaster")))

    def test_an_open_forest_asks_nothing_of_a_child(self):
        # Mido lets a child through to the Deku Tree with the Kokiri Sword and a shield, and the exit opens after
        # the Deku Tree. The forest's own chest holds the only sword a child can use.
        no_sword = ("KF Kokiri Sword Chest", "Green Rupee")
        for location in ("Deku Tree Map Chest", "HC Malon Egg"):
            self.assertFalse(self.reachable(location, no_sword), location)
            self.assertTrue(self.reachable(location, no_sword, settings=FOREST_OPEN), location)

    def test_a_closed_deku_forest_lets_a_child_out_and_keeps_mido(self):
        # The forest's own chest holds the only sword a child can use, and the forest's shop sells the Deku Shield.
        nothing_in_hand = (("KF Kokiri Sword Chest", "Green Rupee"), ("KF Shop Item 1", "Green Rupee"))
        self.assertFalse(self.reachable("HC Malon Egg", *nothing_in_hand))
        self.assertTrue(self.reachable("HC Malon Egg", *nothing_in_hand, settings=CLOSED_DEKU))
        self.assertFalse(self.reachable("Deku Tree Map Chest", *nothing_in_hand, settings=CLOSED_DEKU))
        self.assertTrue(self.reachable("Deku Tree Map Chest", settings=CLOSED_DEKU))

    def test_mido_lets_a_child_by_with_the_hylian_shield(self):
        # Without the forest shop's Deku Shield, and with the way out of the forest open, the child buys the Hylian
        # Shield in the market.
        no_deku_shield = ("KF Shop Item 1", "Green Rupee")
        self.assertTrue(self.reachable("Deku Tree Map Chest", no_deku_shield, settings=CLOSED_DEKU))
        no_sword = ("KF Kokiri Sword Chest", "Green Rupee")
        self.assertFalse(self.reachable("Deku Tree Map Chest", no_sword, settings=CLOSED_DEKU))
        no_hylian_shield = ("Market Bazaar Item 1", "Green Rupee")
        self.assertFalse(self.reachable("Deku Tree Map Chest", no_deku_shield, no_hylian_shield, settings=CLOSED_DEKU))

    def test_an_open_bridge_needs_no_light_arrows(self):
        # Zelda gives the Light Arrows after the Spirit and the Shadow sages wake, and the bridge appears after her.
        no_light_arrows = ("Market ToT Light Arrow Cutscene", "Green Rupee")
        self.assertFalse(self.reachable("Ganon's Castle Forest Trial Chest", no_light_arrows))
        self.assertTrue(self.reachable("Ganon's Castle Forest Trial Chest", no_light_arrows, settings=BRIDGE_OPEN))

    def test_the_tower_opens_without_trials(self):
        # Each trial ends with a Light Arrow shot at its core. The bridge is open so that Link reaches the castle at all.
        tower = "Ganon's Castle Tower Boss Key Chest"
        no_light_arrows = ("Market ToT Light Arrow Cutscene", "Green Rupee")
        self.assertFalse(self.reachable(tower, no_light_arrows, settings=BRIDGE_OPEN))
        self.assertTrue(self.reachable(tower, no_light_arrows, settings=BRIDGE_OPEN | NO_TRIALS))

    def test_no_door_takes_a_key_when_keys_are_not_needed(self):
        # The first room's key opens the first lock. The boss key chest holds the only boss key.
        no_first_key = ("Forest Temple First Room Chest", "Green Rupee")
        no_boss_key = ("Forest Temple Boss Key Chest", "Green Rupee")
        self.assertFalse(self.reachable("Forest Temple First Stalfos Chest", no_first_key))
        self.assertTrue(self.reachable("Forest Temple First Stalfos Chest", no_first_key, settings=KEYS_NOT_NEEDED))
        self.assertFalse(self.reachable("Phantom Ganon", no_boss_key))
        self.assertTrue(self.reachable("Phantom Ganon", no_boss_key, settings=KEYS_NOT_NEEDED))
        no_tower_key = ("Ganon's Castle Tower Boss Key Chest", "Green Rupee")
        self.assertFalse(self.reachable("Ganon", no_tower_key))
        self.assertTrue(self.reachable("Ganon", no_tower_key, settings=KEYS_NOT_NEEDED))

    def test_shadow_temple_hover_boots_chest_needs_no_hover_boots(self):
        # On a first visit the corridor floor falls under Link, and he climbs out of the pit on Dead Hand's side. The
        # Nocturne of Shadow, the only way into the temple, warps him out.
        no_hover_boots = ("Shadow Temple Hover Boots Chest", "Green Rupee")
        self.assertTrue(self.reachable("Shadow Temple Hover Boots Chest", no_hover_boots))

    def test_the_lake_sun_gives_its_item_only_after_the_water_temple(self):
        self.assertTrue(self.reachable("LH Sun"))
        self.assertFalse(self.reachable("LH Sun", ("Ice Cavern Iron Boots Chest", "Green Rupee"),
                                        ("Link's Pocket", "Progressive Hookshot")))

    def test_the_boomerang_fetches_no_heart_piece(self):
        self.assertTrue(self.reachable("Kak Windmill Freestanding PoH"))
        self.assertFalse(self.reachable("Kak Windmill Freestanding PoH", NEVER_ADULT))

    def test_only_a_planted_bean_reaches_the_graveyard_ledge(self):
        self.assertTrue(self.reachable("Graveyard Freestanding PoH"))
        self.assertFalse(self.reachable("Graveyard Freestanding PoH", ("ZR Magic Bean Salesman", "Green Rupee")))

    def test_shadow_temple_fifth_lock_guards_the_boss_key(self):
        # The four other locks stand on the way to the maze, so the boss key room takes all five keys. The door before
        # the boss has no lock.
        self.assertTrue(self.reachable("Shadow Temple Boss Key Chest"))
        boss_key_for_last_key = ("Shadow Temple Invisible Floormaster Chest", "Shadow Temple Boss Key")
        self.assertFalse(self.reachable("Shadow Temple Boss Key Chest", boss_key_for_last_key))
        self.assertTrue(self.reachable("Bongo Bongo", boss_key_for_last_key))

    def test_fire_temple_locks_take_one_key_more_each(self):
        # A new game starts with the lowest locked door open, and seven locks take a key. Four stand before the narrow
        # path room. Its door into the fire wall chase has no lock, so the chase door into the maze is the fifth. The
        # Song of Time cage door and the remake's lock into the Flare Dancer room come last, in either order.
        def without(*chests):
            return [(f"Fire Temple {chest} Chest", "Green Rupee") for chest in chests]
        # The lower boulder maze holds the map. The walkway above it, reached through the chase, holds a key, which
        # cannot open the narrow path room's door before it.
        self.assertTrue(self.reachable("Fire Temple Boulder Maze Upper Chest"))
        self.assertFalse(self.reachable("Fire Temple Boulder Maze Upper Chest", *without("Boulder Maze Side Room")))
        self.assertTrue(self.reachable("Fire Temple Compass Chest", *without("Boulder Maze Shortcut")))
        self.assertFalse(self.reachable("Fire Temple Compass Chest",
                                        *without("Boulder Maze Shortcut", "Boulder Maze Upper")))
        # With a hammer and Hover Boots found outside, Link crosses the maze platforms to the Flare Dancer door without
        # opening the cage door.
        outside = (("KF Mido Top Left Chest", "Megaton Hammer"), ("KF Mido Top Right Chest", "Hover Boots"))
        only_six_keys = without("Compass", "Highest Goron")
        self.assertTrue(self.reachable("Fire Temple Flame Maze Right Pot 1", *outside, *only_six_keys))
        self.assertFalse(self.reachable("Fire Temple Megaton Hammer Chest", *outside, *only_six_keys))
        self.assertTrue(self.reachable("Fire Temple Megaton Hammer Chest", *outside, *without("Highest Goron")))

    # A new game starts with the Water Temple's west middle-floor door open. Three locks take a key, the waterfall,
    # boulders and central pillar doors. Each opens off the main hall, so any of them can be the third opened.

    def test_water_temple_waterfall_door_takes_the_third_key(self):
        # The door at the top of the waterfall room has no lock. Before the temple's own Longshot, Link reaches three
        # keys: the bow target key needs it.
        self.assertTrue(self.reachable("Water Temple Longshot Chest"))
        self.assertFalse(self.reachable("Water Temple Longshot Chest", ("Water Temple Dragon Chest", "Green Rupee")))

    def test_water_temple_boulders_door_takes_the_third_key(self):
        # The door into the boss key room has no lock. A Longshot found outside crosses the spike moat and reaches the
        # bow target key before any lock.
        longshot = ("KF Mido Top Left Chest", "Progressive Hookshot")
        no_dragon_key = ("Water Temple Dragon Chest", "Green Rupee")
        no_torches_key = ("Water Temple Torches Chest", "Green Rupee")
        self.assertTrue(self.reachable("Water Temple Boss Key Chest", longshot, no_dragon_key))
        self.assertFalse(self.reachable("Water Temple Boss Key Chest", longshot, no_dragon_key, no_torches_key))

    def test_water_temple_central_pillar_door_takes_the_third_key(self):
        # With no bow and no fire source, Link raises the water to the middle only through this door, and reaches two
        # keys before it.
        no_fire = (("Forest Temple Bow Chest", "Green Rupee"), ("DMT Great Fairy Din's Fire", "Green Rupee"))
        self.assertFalse(self.reachable("Water Temple Central Pillar Chest", *no_fire))
        self.assertTrue(self.reachable("Water Temple Central Pillar Chest", *no_fire,
                                       ("Water Temple Like Like Chest", "Water Temple Small Key")))

    def test_water_temple_bow_target_chest_needs_the_longshot(self):
        # An arrow in the eye opens the gate to the chest's room, and it shuts before the Hover Boots get Link there.
        self.assertTrue(self.reachable("Water Temple Central Bow Target Chest"))
        self.assertFalse(self.reachable("Water Temple Central Bow Target Chest",
                                        ("Water Temple Longshot Chest", "Green Rupee"),
                                        ("KF Mido Top Left Chest", "Hover Boots")))

    # Chests SoH has no location for.

    def test_ganons_castle_lock_takes_the_key_of_the_light_trial_chest(self):
        # The castle's one small key lies in an invisible chest in the Light Trial's Lullaby room, before its one lock.
        # Every trial, the Light Trial's behind that lock among them, stands before the tower.
        no_key = ("Ganon's Castle Light Trial Triforce Room Invisible Chest", "Green Rupee")
        self.assertTrue(self.reachable("Ganon's Castle Light Trial Triforce Room Invisible Chest",
                                       SILVER_GAUNTLETS_AT_START))
        self.assertTrue(self.reachable("Ganon's Castle Tower Boss Key Chest", SILVER_GAUNTLETS_AT_START))
        self.assertFalse(self.reachable("Ganon's Castle Tower Boss Key Chest", SILVER_GAUNTLETS_AT_START, no_key))

    def test_the_treasure_chest_games_prize_needs_the_lens_under_every_setting(self):
        # Each room deals its key to one of two chests at random, and the Lens shows which. Without it a win takes five
        # lucky picks.
        self.assertTrue(self.reachable("Market Treasure Chest Game Reward"))
        self.assertFalse(self.reachable("Market Treasure Chest Game Reward", NO_LENS, settings=LENS_NEVER_NEEDED))

    def test_the_hookshot_hits_the_eyes_that_take_it(self):
        # Most of the remake's eye switches take the hookshot, and a few keep to the bow.
        # The Gerudo card lets Link past the fortress's guards without the bow or the hookshot.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        card = ("KF Mido Bottom Right Chest", "Gerudo Membership Card")
        self.assertTrue(self.reachable("Gerudo Training Ground Lobby Right Chest", no_bow))
        self.assertFalse(self.reachable("Gerudo Training Ground Lobby Right Chest", no_bow, NO_HOOKSHOT, card))
        self.assertFalse(self.reachable("Gerudo Training Ground Eye Statue Chest", no_bow))
        # The hammer room's door to the eye statue's lower room opens to its eye alone. Without the Lens, the way down
        # from above is shut, so that door is the only way in.
        self.assertTrue(self.reachable("Gerudo Training Ground Eye Statue Chest", NO_LENS, NO_HAMMER))

    def test_adult_link_walks_between_the_colossus_hands(self):
        # From the Mirror Shield hand an adult walks to the Silver Gauntlets hand, so he needs neither the Longshot nor
        # the fifth key there. The Hover Boots cross the wasteland in the Longshot's place.
        four_keys = ("Spirit Temple Near Four Armos Chest", "Green Rupee")
        hover_boots_no_longshot = (("Water Temple Longshot Chest", "Green Rupee"),
                                   ("KF Mido Bottom Left Chest", "Hover Boots"))
        self.assertTrue(self.reachable("Spirit Temple Silver Gauntlets Chest", SILVER_GAUNTLETS_AT_START, four_keys,
                                       *hover_boots_no_longshot))

    def test_forest_temple_alcove_door_opens_only_after_the_courtyard_ledge(self):
        # The door from the block push room's alcove to the north-west courtyard's ledge stays barred from the alcove
        # until Link has come through it from the ledge, which he first reaches from the boss key hallway with the bow.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        hover_boots = ("KF Mido Top Left Chest", "Hover Boots")
        self.assertTrue(self.reachable("Forest Temple Floormaster Chest"))
        self.assertFalse(self.reachable("Forest Temple Floormaster Chest", no_bow, hover_boots))

    def test_forest_temple_blue_poe_room_east_door_waits_for_beth(self):
        # The east door opens once Beth dies, and she comes out only to the bow. Din's Fire would melt the frozen eye
        # beyond the door in the bow's place. Two of the temple's keys lie behind the bow, so two more lie outside.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        two_keys = (("KF Mido Top Left Chest", "Forest Temple Small Key"),
                    ("KF Mido Top Right Chest", "Forest Temple Small Key"))
        self.assertFalse(self.reachable("Forest Temple Falling Ceiling Room Chest", no_bow, *two_keys))

    def test_a_childs_boomerang_hits_the_near_eyes(self):
        # Without the slingshot, a child's boomerang hits the Deku Tree's basement eye on the way to Gohma, its
        # slingshot room's eye that drops the side chest's ladder, and the eye across the fire in Dodongo's Cavern's
        # first eye room. The second eye room's far eye stays out of its reach.
        no_slingshot = ("Deku Tree Slingshot Chest", "Green Rupee")
        boomerang = ("KF Mido Top Left Chest", "Boomerang")
        self.assertTrue(self.reachable("Queen Gohma", no_slingshot, boomerang))
        self.assertTrue(self.reachable("Deku Tree Slingshot Room Side Chest", no_slingshot, boomerang))
        self.assertFalse(self.reachable("Queen Gohma", no_slingshot))
        self.assertTrue(self.reachable("Dodongos Cavern Upper Lizalfos Chest", no_slingshot, boomerang, NEVER_ADULT))
        self.assertFalse(self.reachable("Dodongos Cavern Bomb Bag Chest", no_slingshot, boomerang, NEVER_ADULT))

    def test_the_longshot_opens_the_water_temple_eye_gates(self):
        # The Longshot hits the eye over the gate to the central bow target room, with the water at middle, and the eye
        # over the dragon room's gate from the river, and gets Link through each before it shuts. Past the dragon room's
        # gate lies SoH's River Chest location, which no chest of the remake stands for.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        self.assertTrue(self.reachable("Water Temple Central Bow Target Chest", no_bow))
        self.assertTrue(self.reachable("Water Temple River Chest", no_bow))
        # At low water the central gate keeps the bow. With the Longshot from the start and neither the bow nor Din's
        # Fire, the logic never gets the water to middle.
        longshot_early = ("Link's Pocket", "Progressive Hookshot")
        no_dins_fire = ("DMT Great Fairy Din's Fire", "Green Rupee")
        self.assertFalse(self.reachable("Water Temple Central Bow Target Chest", no_bow, no_dins_fire, longshot_early))

    # Chests SoH has no location for.

    def test_lost_woods_bridge_chest_needs_the_bridge_bean_as_adult(self):
        # Adult Link rides the platform that grows from the magic bean planted by the bridge.
        self.assertTrue(self.reachable("LW Bridge Chest"))
        self.assertFalse(self.reachable("LW Bridge Chest", ("ZR Magic Bean Salesman", "Green Rupee")))
        self.assertFalse(self.reachable("LW Bridge Chest", NEVER_ADULT))

    def test_kakariko_lookout_chest_needs_the_longshot(self):
        self.assertTrue(self.reachable("Kak Lookout Chest"))
        self.assertFalse(self.reachable("Kak Lookout Chest", ("Water Temple Longshot Chest", "Green Rupee")))

    def test_zoras_fountain_scarecrow_chest_needs_the_hookshot(self):
        self.assertTrue(self.reachable("ZF Scarecrow Chest"))
        self.assertFalse(self.reachable("ZF Scarecrow Chest", NO_HOOKSHOT))

    def test_gerudo_valley_storms_grotto_chests_need_the_song_of_storms(self):
        for chest in ("GV Storms Grotto Chest", "GV Storms Grotto Visible Chest"):
            self.assertTrue(self.reachable(chest))
            self.assertFalse(self.reachable(chest, ("Song from Windmill", "Green Rupee")))

    def test_gerudo_valley_octorok_grotto_chest_needs_the_iron_boots(self):
        # The chest stands under water, in the grotto under a boulder that takes the Silver Gauntlets.
        self.assertTrue(self.reachable("GV Octorok Grotto Chest", SILVER_GAUNTLETS_AT_START))
        no_iron_boots = ("Ice Cavern Iron Boots Chest", "Green Rupee")
        self.assertFalse(self.reachable("GV Octorok Grotto Chest", SILVER_GAUNTLETS_AT_START, no_iron_boots))

    def test_gerudo_fortress_grotto_chest_is_reachable(self):
        self.assertTrue(self.reachable("GF Grotto Chest"))

    def test_sacred_forest_meadow_song_of_time_grotto_chest_needs_the_song_of_time(self):
        # Song of Time blocks stand before the chest, in the grotto under a rock near Saria.
        self.assertTrue(self.reachable("SFM Song of Time Grotto Chest"))
        self.assertFalse(self.reachable("SFM Song of Time Grotto Chest", ("Song from Ocarina of Time", "Green Rupee")))

    def test_sacred_forest_meadow_wolfos_grotto_opens_to_the_song_of_storms(self):
        # A cover that goes at the Song of Storms hides the grotto, and no bomb opens it. A Link who never grows up
        # holds bombs and never learns the song.
        self.assertTrue(self.reachable("SFM Wolfos Grotto Chest"))
        self.assertFalse(self.reachable("SFM Wolfos Grotto Chest", NEVER_ADULT))

    def test_zoras_river_tree_skulltula_comes_out_of_soil_for_bugs(self):
        self.assertTrue(self.reachable("ZR GS Tree"))
        self.assertFalse(self.reachable("ZR GS Tree", *NO_BOTTLE))

    def test_hyrule_field_cow_grotto_skulltula_is_in_the_south_grotto_after_a_childs_kill(self):
        self.assertTrue(self.reachable("HF GS Cow Grotto"))
        self.assertFalse(self.reachable("HF GS Cow Grotto", NEVER_ADULT))

    def test_a_high_skulltula_s_token_is_taken_from_afar(self):
        # It hangs high with nothing beside it to climb, and its token stays at its web.
        self.assertTrue(self.reachable("LLR GS Rain Shed"))
        self.assertFalse(self.reachable("LLR GS Rain Shed", NO_BOOMERANG))

    def test_dodongos_cavern_vines_skulltula_needs_a_projectile(self):
        # It hangs on a climbing net across from the top of the stairs, out of the sword's reach.
        self.assertTrue(self.reachable("Dodongos Cavern GS Vines Above Stairs", *NO_RANGED))
        self.assertFalse(self.reachable("Dodongos Cavern GS Vines Above Stairs", *NO_RANGED, *NO_SLINGSHOT_OR_BOW))

    def test_dodongos_cavern_alcove_skulltula_is_behind_a_cracked_wall_off_the_balcony(self):
        # A bomb opens the wall, and the sword kills the skulltula from the room's floor.
        self.assertTrue(self.reachable("Dodongos Cavern GS Alcove Above Stairs", *NO_RANGED))
        self.assertFalse(self.reachable("Dodongos Cavern GS Alcove Above Stairs",
                                        ("Dodongos Cavern Bomb Bag Chest", "Green Rupee"), NO_HAMMER))

    def test_water_temple_falling_platform_room_skulltula_is_beyond_the_cracked_wall(self):
        # It hangs beside the ledge of the room behind the east tower's cracked wall, where the sword kills it.
        self.assertTrue(self.reachable("Water Temple GS Falling Platform Room",
                                       ("Water Temple Longshot Chest", "Green Rupee")))

    def test_lon_lon_ranch_back_wall_skulltula_falls_from_a_field_tree(self):
        # A roll into the tree by the Big Poe fences drops it, by day or night, and the sword kills it.
        self.assertTrue(self.reachable("LLR GS Back Wall", *NO_RANGED))

    def test_deku_theater_mask_of_truth_asks_for_no_stone(self):
        # The running man buys the Bunny Hood once the mask quest has lent it, whatever stones Link holds.
        no_stones = (("Queen Gohma", "Green Rupee"), ("King Dodongo", "Green Rupee"), ("Barinade", "Green Rupee"))
        self.assertTrue(self.reachable("LW Deku Theater Mask of Truth", *no_stones))

    def test_chests_behind_explosives_a_beamos_or_the_climb_room_are_reachable(self):
        # A cracked wall, the well's rocks, a Beamos and the Spirit Temple's climb room door stand before them. Every
        # adult holds bombs, since King Dodongo falls only to explosives. The Colossus grotto lies under a boulder that
        # takes the Silver Gauntlets, and the Training Ground's heavy block room is past the gauntlets and the Lens.
        heavy_block_room = (("KF Mido Top Right Chest", "Silver Gauntlets"),
                            ("KF Mido Bottom Right Chest", "Lens of Truth"))
        for chest, items in (("Colossus Grotto Chest", (SILVER_GAUNTLETS_AT_START,)),
                             ("Gerudo Training Ground Heavy Block Cracked Wall Chest", heavy_block_room),
                             ("Gerudo Training Ground Beamos Chest", ()), ("Bottom of the Well Map Chest", ()),
                             ("Spirit Temple Map Chest", ())):
            self.assertTrue(self.reachable(chest, *items), chest)

    def test_hyrule_field_poe_chests_need_adult_link(self):
        for chest in ("HF South Poe Chest", "HF North Poe Chest"):
            self.assertTrue(self.reachable(chest))
            self.assertFalse(self.reachable(chest, NEVER_ADULT))

    def test_hyrule_field_southwest_poe_chest_needs_the_lake_refilled(self):
        # Beating Morpha refills the lake, and Morpha's door takes the Water Temple's boss key.
        self.assertTrue(self.reachable("HF Southwest Poe Chest"))
        self.assertFalse(self.reachable("HF Southwest Poe Chest", ("Water Temple Boss Key Chest", "Green Rupee")))

    def test_hyrule_field_redead_grotto_chest_needs_the_song_of_storms(self):
        self.assertTrue(self.reachable("HF Redead Grotto Chest"))
        self.assertFalse(self.reachable("HF Redead Grotto Chest", ("Song from Windmill", "Green Rupee")))

    def test_hyrule_field_fairy_thought_chest_needs_both_ages(self):
        # Adult Link reads the Fairy Thought beside the chest, and the chest then opens for child Link.
        self.assertTrue(self.reachable("HF Fairy Thought Chest"))
        self.assertFalse(self.reachable("HF Fairy Thought Chest", NEVER_ADULT))

    def test_lake_hylia_grotto_chest_needs_fire_for_four_candles(self):
        # A child lights the candles with Din's Fire and a Deku Stick, an adult with Din's Fire or fire arrows alone.
        no_dins_fire = ("DMT Great Fairy Din's Fire", "Green Rupee")
        no_fire_arrows = ("LH Sun", "Green Rupee")
        self.assertTrue(self.reachable("LH Grotto Chest", no_dins_fire))
        self.assertFalse(self.reachable("LH Grotto Chest", no_dins_fire, no_fire_arrows))

    def test_hyrule_field_tower_chest_needs_the_hookshot(self):
        self.assertTrue(self.reachable("HF Tower Chest", ("Water Temple Longshot Chest", "Green Rupee")))
        self.assertFalse(self.reachable("HF Tower Chest", NO_HOOKSHOT))

    def test_crater_hammer_grotto_chest_needs_the_hammer(self):
        self.assertTrue(self.reachable("DMC Hammer Grotto Chest"))
        self.assertFalse(self.reachable("DMC Hammer Grotto Chest", NO_HAMMER))

    def test_dodongos_cavern_lobby_alcove_chest_needs_a_blast(self):
        # Beside the map chest, behind a cracked wall that no bomb flower stands near. The bracelet alone opens the way
        # to the map chest, and only Dodongo's Cavern holds a bomb bag.
        no_blast = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"), NO_HAMMER)
        self.assertTrue(self.reachable("Dodongos Cavern Lobby Alcove Chest"))
        self.assertTrue(self.reachable("Dodongos Cavern Map Chest", *no_blast))
        self.assertFalse(self.reachable("Dodongos Cavern Lobby Alcove Chest", *no_blast))

    def test_dodongos_cavern_stairs_niche_chest_needs_a_blast(self):
        # At the top of a ladder above the staircase, behind a cracked wall. Link cannot climb a ladder holding a bomb
        # flower, which is enough to drop the staircase and pass the Armos room to the compass chest.
        no_blast = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"), NO_HAMMER)
        self.assertTrue(self.reachable("Dodongos Cavern Stairs Niche Chest"))
        self.assertTrue(self.reachable("Dodongos Cavern Compass Chest", *no_blast))
        self.assertFalse(self.reachable("Dodongos Cavern Stairs Niche Chest", *no_blast))

    def test_jabu_jabus_belly_pot_island_chest_needs_the_boomerang(self):
        # The boomerang stills the swaying floor that carries a child to the island.
        self.assertTrue(self.reachable("Jabu Jabus Belly Pot Island Chest"))
        self.assertFalse(self.reachable("Jabu Jabus Belly Pot Island Chest",
                                        ("Jabu Jabus Belly Boomerang Chest", "Green Rupee")))

    def test_ice_cavern_block_ledge_chest_is_reachable(self):
        self.assertTrue(self.reachable("Ice Cavern Block Ledge Chest"))

    def test_bottom_of_the_well_chest_before_dead_hand_is_reachable(self):
        self.assertTrue(self.reachable("Bottom of the Well Before Dead Hand Chest"))

    def test_fire_temple_scarecrow_ledge_chests_need_what_the_scarecrow_chest_needs(self):
        # Both stand beside the Scarecrow Chest from the start. The way up takes the scarecrow's song and a hookshot.
        for chest in ("Fire Temple Scarecrow Ledge Left Chest", "Fire Temple Scarecrow Ledge Right Chest"):
            self.assertTrue(self.reachable(chest))
            self.assertFalse(self.reachable(chest, NO_HOOKSHOT))

    def test_meadow_song_of_time_grotto_stone_needs_the_song_of_time(self):
        # The stone stands beside the grotto's chest, past the same Song of Time blocks.
        stone = "SFM Song of Time Grotto Gossip Stone"
        self.assertTrue(self.reachable(stone))
        self.assertFalse(self.reachable(stone, ("Song from Ocarina of Time", "Green Rupee")))

    def test_trail_cow_grotto_stone_opens_to_a_childs_bomb(self):
        # A bomb or the hammer opens the grotto. The hammer is an adult's, and every adult holds bombs.
        stone = "DMT Cow Grotto Gossip Stone"
        self.assertTrue(self.reachable(stone, NEVER_ADULT))
        self.assertFalse(self.reachable(stone, *NO_BOMB_BAG))

    def test_kakariko_beehive_grotto_stone_opens_to_a_childs_bomb(self):
        # A bomb or the hammer opens the grotto. The hammer is an adult's, and every adult holds bombs.
        stone = "Kak Beehive Grotto Gossip Stone"
        self.assertTrue(self.reachable(stone, NEVER_ADULT))
        self.assertFalse(self.reachable(stone, *NO_BOMB_BAG))

    def test_river_storms_grotto_stone_needs_the_song_of_storms(self):
        stone = "ZR Storms Grotto Gossip Stone"
        self.assertTrue(self.reachable(stone))
        self.assertFalse(self.reachable(stone, ("Song from Windmill", "Green Rupee")))

    def test_field_east_grotto_stone_opens_to_a_childs_bomb(self):
        # A bomb or the hammer opens the grotto. The hammer is an adult's, and every adult holds bombs.
        stone = "HF East Grotto Gossip Stone"
        self.assertTrue(self.reachable(stone, NEVER_ADULT))
        self.assertFalse(self.reachable(stone, *NO_BOMB_BAG))

    def test_field_south_grotto_stone_opens_to_a_childs_bomb(self):
        # A bomb or the hammer opens the grotto. The hammer is an adult's, and every adult holds bombs.
        stone = "HF South Grotto Gossip Stone"
        self.assertTrue(self.reachable(stone, NEVER_ADULT))
        self.assertFalse(self.reachable(stone, *NO_BOMB_BAG))


if __name__ == "__main__":
    reach.PROGRAM = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
