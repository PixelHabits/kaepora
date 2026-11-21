cmake_minimum_required(VERSION 3.28)
# Lichii needs macOS 15 or later, so the programs need no older one. project() reads this, so it comes first.
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "The oldest macOS the programs run on")
project(kaepora VERSION 0.4.0 LANGUAGES C CXX) # x-release-please-version

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The code uses no C++ modules. Scanning for them would need clang-scan-deps, which Ubuntu packages apart from Clang.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# Release builds keep assert on, so a broken invariant stops the program instead of writing a bad seed.
add_compile_options(-UNDEBUG)

if(WIN32 OR APPLE)
    # Windows has no packages of these, and the macOS package must not need Homebrew's. EXCLUDE_FROM_ALL keeps their
    # install rules out of the package.
    include(FetchContent)
    set(ZSTD_BUILD_SHARED OFF)
    set(ZSTD_BUILD_PROGRAMS OFF)
    set(ZSTD_BUILD_TESTS OFF)
    FetchContent_Declare(nlohmann_json EXCLUDE_FROM_ALL SYSTEM
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
    FetchContent_Declare(spdlog EXCLUDE_FROM_ALL SYSTEM
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744)
    FetchContent_Declare(zstd EXCLUDE_FROM_ALL SYSTEM
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        SOURCE_SUBDIR build/cmake)
    FetchContent_MakeAvailable(nlohmann_json spdlog zstd)
    set(ZSTD_LIBRARY libzstd_static)
else()
    find_package(nlohmann_json REQUIRED)
    find_package(spdlog REQUIRED)
    find_package(zstd REQUIRED)
    # zstd before 1.5.6, as on Ubuntu 24.04, names only its shared and static targets.
    set(ZSTD_LIBRARY zstd::libzstd_shared)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

include(shipwright/CMake/soh-cvars.cmake)

# shipwright/ holds only the upstream files this build reads, so every source in it is compiled.
file(GLOB_RECURSE SHIPWRIGHT_SOURCES CONFIGURE_DEPENDS shipwright/*.c shipwright/*.cpp)

# Everything but the generator programs' main files, compiled once for all of them.
add_library(kaepora-generator OBJECT
    generator/cvars.cpp
    generator/game_state.cpp
    generator/generate.cpp
    generator/hints.cpp
    generator/remake.cpp
    generator/seed_settings.cpp
    generator/setup.cpp
    $<IF:$<PLATFORM_ID:Windows>,generator/traps_windows.cpp,generator/traps.cpp>
    ${SHIPWRIGHT_SOURCES}
)
# configure_file rewrites version.h only when its text changes, so a new version rebuilds only its includers.
configure_file(version.h.in include/version.h @ONLY)
target_include_directories(kaepora-generator PUBLIC
    .
    ${PROJECT_BINARY_DIR}/include
    shipwright/soh/include
    shipwright/soh/src
    shipwright/soh/assets
    shipwright/soh
    shipwright/libultraship/include
)
# functions.h needs a level for game prints. 6 turns them off, as in SoH.
target_compile_definitions(kaepora-generator PUBLIC LOG_LEVEL_GAME_PRINTS=6)
# The item icon table drops const from texture names. The generator only reads the slot tables next to it.
set_source_files_properties(shipwright/soh/src/code/z_inventory.c PROPERTIES COMPILE_OPTIONS
    $<IF:$<C_COMPILER_ID:Clang,AppleClang>,-Wno-incompatible-pointer-types-discards-qualifiers,-Wno-discarded-qualifiers>)
# GCC and Clang take #embed in C++20 as an extension and warn at each use.
set_source_files_properties(generator/remake.cpp patcher/mod.cpp shipwright/soh/soh/Enhancements/Lang/Lang.cpp
    PROPERTIES COMPILE_OPTIONS $<IF:$<CXX_COMPILER_ID:Clang,AppleClang>,-Wno-c23-extensions,-Wno-c++26-extensions>)
target_link_libraries(kaepora-generator PUBLIC nlohmann_json::nlohmann_json spdlog::spdlog)

add_executable(kaepora-seedgen generator/main.cpp)
target_link_libraries(kaepora-seedgen PRIVATE kaepora-generator)

# Answers whether a location can be reached in a given world, for the logic tests.
add_executable(kaepora-reach generator/reach.cpp)
target_link_libraries(kaepora-reach PRIVATE kaepora-generator)

# Designated initializers leave out the members that start empty, which -Wextra would report. clang-cl reads -Wall as
# -Weverything, and its /W4 is Clang's -Wall -Wextra.
if(MSVC)
    set(WARNINGS /W4 -Wpedantic -Wno-missing-field-initializers)
else()
    set(WARNINGS -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

add_library(kaepora-patch-files STATIC patcher/bfevfl.cpp patcher/byml.cpp patcher/compression.cpp
                                       patcher/game_file.cpp patcher/program_patch.cpp patcher/mod.cpp patcher/msbt.cpp
                                       patcher/new_actor.cpp patcher/sarc.cpp)
target_include_directories(kaepora-patch-files PUBLIC patcher)
target_link_libraries(kaepora-patch-files PUBLIC nlohmann_json::nlohmann_json PRIVATE ${ZSTD_LIBRARY})
target_compile_options(kaepora-patch-files PRIVATE ${WARNINGS})

add_executable(kaepora-patch patcher/main.cpp)
target_link_libraries(kaepora-patch PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch PRIVATE ${WARNINGS})

add_executable(kaepora-patch-roundtrip tests/byml_roundtrip.cpp)
target_link_libraries(kaepora-patch-roundtrip PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-roundtrip PRIVATE ${WARNINGS})

add_executable(kaepora-patch-flow-roundtrip tests/bfevfl_roundtrip.cpp)
target_link_libraries(kaepora-patch-flow-roundtrip PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-flow-roundtrip PRIVATE ${WARNINGS})

add_executable(kaepora-patch-pack-roundtrip tests/pack_roundtrip.cpp)
target_link_libraries(kaepora-patch-pack-roundtrip PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-pack-roundtrip PRIVATE ${WARNINGS})

add_executable(kaepora-patch-text-roundtrip tests/text_roundtrip.cpp)
target_link_libraries(kaepora-patch-text-roundtrip PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-text-roundtrip PRIVATE ${WARNINGS})

add_executable(kaepora-patch-value-test tests/byml_value_test.cpp)
target_link_libraries(kaepora-patch-value-test PRIVATE kaepora-patch-files)
target_compile_options(kaepora-patch-value-test PRIVATE ${WARNINGS})

# Makes seed folders with the window's MakeSeed, for the tests.
add_executable(kaepora-make-seed tests/make_seed.cpp kaepora/seed.cpp)
target_link_libraries(kaepora-make-seed PRIVATE kaepora-generator kaepora-patch-files)
target_compile_options(kaepora-make-seed PRIVATE ${WARNINGS})

# The window. Qt is found quietly, so everything else still builds where Qt is missing.
find_package(Qt6 QUIET COMPONENTS Widgets Network OPTIONAL_COMPONENTS Test)
if(Qt6_FOUND)
    add_executable(kaepora kaepora/main.cpp kaepora/new_version.cpp kaepora/seed.cpp kaepora/settings_dialog.cpp
                           kaepora/window.cpp $<$<PLATFORM_ID:Windows>:kaepora/kaepora.rc>)
    target_link_libraries(kaepora PRIVATE kaepora-generator kaepora-patch-files Qt6::Widgets Qt6::Network)
    target_compile_options(kaepora PRIVATE ${WARNINGS})
    set_target_properties(kaepora PROPERTIES WIN32_EXECUTABLE ON)

    # The window is the one program that installs, with the licences of the code built into it. On Windows it sits at
    # the top of the folder, beside the Visual C++ runtime, as Windows looks for a program's libraries in its own folder
    # first. tools/package-windows.sh adds Qt there. On macOS it is the app Kaepora.app, and tools/package-macos.sh
    # adds Qt to it.
    if(WIN32)
        set(LICENSES licenses)
        install(TARGETS kaepora DESTINATION .)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
        include(InstallRequiredSystemLibraries)
    elseif(APPLE)
        set(LICENSES Kaepora.app/Contents/Resources/licenses)
        set(ICON dist/org.kaepora.Kaepora.icns)
        set_source_files_properties(${ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(kaepora PRIVATE ${ICON})
        set_target_properties(kaepora PROPERTIES MACOSX_BUNDLE ON OUTPUT_NAME Kaepora
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/dist/Info.plist.in
            INSTALL_RPATH @executable_path/../Frameworks)
        install(TARGETS kaepora BUNDLE DESTINATION .)
    else()
        include(GNUInstallDirs)
        set(LICENSES ${CMAKE_INSTALL_DATADIR}/licenses)
        install(TARGETS kaepora)
        install(FILES dist/org.kaepora.Kaepora.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES dist/org.kaepora.Kaepora.svg
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
        install(FILES dist/org.kaepora.Kaepora.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    endif()
    if(WIN32 OR APPLE)
        install(FILES ${nlohmann_json_SOURCE_DIR}/LICENSE.MIT DESTINATION ${LICENSES}/nlohmann_json)
        install(FILES ${spdlog_SOURCE_DIR}/LICENSE DESTINATION ${LICENSES}/spdlog)
        install(FILES ${spdlog_SOURCE_DIR}/include/spdlog/fmt/bundled/fmt.license.rst DESTINATION ${LICENSES}/fmt)
        install(FILES ${zstd_SOURCE_DIR}/LICENSE DESTINATION ${LICENSES}/zstd)
    endif()
    install(FILES LICENSE DESTINATION ${LICENSES}/kaepora)
    install(FILES shipwright/libultraship/LICENSE DESTINATION ${LICENSES}/libultraship)
    install(FILES shipwright/soh/soh/Enhancements/randomizer/3drando/LICENSE.md DESTINATION ${LICENSES}/oot-randomizer)
endif()

enable_testing()
add_test(NAME upstream-tool COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/upstream_test.py)
add_test(NAME patch-byml-value COMMAND kaepora-patch-value-test)
foreach(TEST IN ITEMS only_shuffled_chests_change plan_lists_each_shuffled_chest plan_matches_spoiler
                     spoiler_log_lists_the_shuffled_locations_and_the_route route_names_no_gold_skulltula_token
                     route_names_each_boss_before_the_steps_that_wait_for_it
                     a_route_with_the_forest_exit_open_may_never_enter_the_deku_tree
                     route_takes_what_link_is_handed_over_what_he_buys route_steps_are_in_reach_in_their_sphere
                     a_route_that_goes_back_to_child_before_the_forest_temple_is_in_reach_in_its_sphere
                     a_route_warps_a_child_to_the_colossus_only_after_a_song_that_takes_him_out
                     plan_opens_the_doors_the_rules_count_open plan_keeps_quests_from_going_back
                     same_seed_same_files twenty_seeds_generate spirit_dungeon_items_stay_out_of_the_colossus
                     scrub_seeds_and_the_large_magic_jar_are_placed a_chest_kept_twice_holds_one_item
                     loose_items_the_remake_keeps_in_chests_are_shuffled
                     dins_fire_comes_from_the_summit_great_fairy
                     spoiler_log_records_the_settings default_settings_are_the_settings_left_out
                     an_unknown_setting_is_refused a_price_a_box_cannot_hold_or_a_range_upside_down_is_refused
                     a_price_setting_nothing_applies_is_refused the_spoiler_log_holds_only_the_prices_in_effect
                     a_dungeon_left_out_keeps_its_chests keys_stay_in_their_own_chests
                     keys_not_needed forest_open forest_closed_deku only_closed_deku_removes_midos_castle_entry
                     bridge_always_open no_trials
                     maps_and_compasses_stay_in_their_own_chests maps_and_compasses_given_at_the_start
                     lens_never_needed_leaves_the_lens_off_the_route
                     each_count_shuffles_the_first_goods_of_every_shop_in_sohs_order
                     a_random_number_per_shop_shuffles_one_to_five_of_its_first_goods
                     vanilla_prices_are_each_slots_own balanced_prices_go_past_cheap_ones one_price_for_every_slot
                     prices_between_two_prices shops_sell_no_dungeon_item_and_nothing_a_shop_may_refuse
                     a_route_that_buys_from_shops_is_in_reach_in_its_sphere hints_name_shop_goods_truly
                     hints_name_the_goods_only_five_per_shop_shuffle
                     gifts_shuffled a_gift_given_at_two_sites_gives_one_item gifts_give_no_dungeon_item
                     what_a_shop_may_refuse_goes_to_any_other_shuffled_place
                     a_gift_can_hold_the_bow_and_the_slingshot
                     a_route_that_takes_gifts_is_in_reach_in_its_sphere hints_name_gifts_truly
                     dungeon_rewards_shuffled stones_and_medallions_go_to_chests_gifts_and_the_nine_places
                     a_dungeon_left_out_keeps_its_boss_reward
                     hints_name_no_stone_or_medallion_as_an_item_to_find hints_name_dungeon_rewards_truly
                     gold_skulltulas_shuffled skulltulas_hold_the_bomb_bag_the_bow_and_the_slingshot
                     a_dungeon_left_out_keeps_its_skulltulas
                     a_route_with_gold_skulltulas_shuffled_is_in_reach_in_its_sphere
                     hints_name_the_place_a_skulltula_stands_in hints_name_gold_skulltulas_truly
                     loose_items_shuffled loose_keys_follow_the_keys_setting
                     loose_items_hold_the_bomb_bag_the_bow_and_the_slingshot
                     a_dungeon_left_out_keeps_its_loose_items
                     a_route_with_loose_items_shuffled_is_in_reach_in_its_sphere hints_name_loose_items_truly
                     songs_shuffled_among_themselves a_route_that_learns_songs_at_other_places_is_in_reach_in_its_sphere
                     hints_name_song_places_truly a_route_reaches_the_colossus_only_with_a_way_out
                     every_setting_changed_at_once seed_text_ignores_case every_hint_is_true
                     every_stone_has_a_hint_that_fits_its_boxes hints_never_move_an_item fixed_seed_matches_fixture
                     the_chest_game_owner_names_the_prize every_message_fits_its_box
                     mido_names_where_the_kokiri_sword_lies a_place_a_character_names_is_not_named_again_by_a_stone
                     mido_gives_no_hint_with_the_forest_open characters_give_no_hints_at_vanilla
                     with_songs_shuffled_both_stone_bosses_wait_for_zeldas_letter
                     with_songs_shuffled_shadow_and_spirit_never_both_come_before_the_forest_temple
                     with_songs_shuffled_the_courtyard_scene_starts_played
                     a_seed_starts_with_what_is_chosen the_opening_is_skipped_unless_played
                     cutscenes_are_skipped_unless_played)
    add_test(NAME ${TEST} COMMAND ${Python3_EXECUTABLE} -I -B ${PROJECT_SOURCE_DIR}/tests/seedgen_test.py
                                  $<TARGET_FILE:kaepora-seedgen> $<TARGET_FILE:kaepora-reach> ${PROJECT_VERSION}
                                  SeedgenTest.test_${TEST})
endforeach()
add_test(NAME logic COMMAND ${Python3_EXECUTABLE} -I -B ${PROJECT_SOURCE_DIR}/tests/logic_test.py
                            $<TARGET_FILE:kaepora-reach>)

if(Qt6Test_FOUND)
    add_executable(kaepora-window-test tests/window_test.cpp kaepora/new_version.cpp kaepora/seed.cpp
                                       kaepora/settings_dialog.cpp kaepora/window.cpp)
    set_target_properties(kaepora-window-test PROPERTIES AUTOMOC ON)
    target_link_libraries(kaepora-window-test PRIVATE kaepora-generator kaepora-patch-files Qt6::Widgets Qt6::Network
                                                      Qt6::Test)
    target_compile_options(kaepora-window-test PRIVATE ${WARNINGS})
    # The window's tests that make no seed need no game file.
    set(WINDOW_TESTS lichiisModsFolderIsTheDefault theChoicesOfEachColumnLineUp whatJoinsTheShuffleHasItsOwnGroup
                     theHintsChoicesShareAGroup aChoiceShowsInTheSummary defaultsSetsEveryControlBack
                     theSummaryNamesUpToThreeDungeonsLeftOut shopGoodsOffersEachCountUpToFiveAndARandomNumber
                     shopPricesWaitForShopGoodsAndShowOnlyTheNumbersOfTheirChoice shopPricesAreRememberedAndShowInTheDetails
                     goodsBackAtVanillaLeaveNoPriceChoice rememberedPricesWithVanillaGoodsStartAsTheDefaults
                     longSettingsLeaveTheWindowWidth aSeedMadeBeforeHintsFromCharactersHasNone
                     aSeedMadeBeforeTheOpeningSettingPlaysIt aSeedMadeBeforeTheCutscenesSettingPlaysThem
                     aSeedWhoseSpoilerLogCannotBeReadIsListed settingsAreRemembered
                     unreadableRememberedSettingsStartAsTheDefaults theWindowShowsTheVersionRunning
                     aNewerVersionShowsTheNotice quietOutcomesShowNothing theButtonsStayInAShortWindow
                     theBoxTurnsTheCheckOff aDayPassesBetweenRequests aNewerVersionIsRememberedUntilItRuns
                     untickingStopsARequestInFlight theStartWithListHasTheItems
                     oneItemToStartWithIsOneItem cancelLeavesTheItemsToStartWith defaultsClearsTheItemsToStartWith)
    if(APPLE)
        list(APPEND WINDOW_TESTS playStartsTheProgramLichiisAppNames)
    endif()
    foreach(TEST IN LISTS WINDOW_TESTS)
        add_test(NAME window_${TEST} COMMAND kaepora-window-test ${TEST})
        # On Windows, QTest writes to the debugger rather than to stderr when no console is attached, as under ctest.
        set_tests_properties(window_${TEST} PROPERTIES ENVIRONMENT
                             "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1")
    endforeach()
endif()

# The game's container is Nintendo's data and stays out of git, so the tests that read it need its path.
set(KAEPORA_GAME_NUP "" CACHE FILEPATH "The game's .nup container, read by the patcher tests")
set(KAEPORA_TESTED_MOD "" CACHE FILEPATH "The placement file of the chest-swap mod tested in the game")
set(KAEPORA_TESTED_START_MOD "" CACHE FILEPATH "The game data list of the start-flag mod tested in the game")
set(KAEPORA_TESTED_SUBSTITUTE_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with one item in a chest's item and substitute values")
set(KAEPORA_TESTED_ADDED_ITEM_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with an item value added to two chests that named none")
set(KAEPORA_TESTED_HAMMER_ROOM_MOD "" CACHE FILEPATH
    "The placement file of the mod tested in the game with the red rupee in the Training Ground hammer room chest")
set(KAEPORA_TESTED_GIFT_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with the Boomerang in the potion shop child's bottle gift")
set(KAEPORA_TESTED_FIRST_GIFTS_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with other items in eight gifts and four gift items in chests")
set(KAEPORA_TESTED_EVENT_TEXT_MODS "" CACHE STRING
    "The romfs folders of the mods tested in the game with the event texts every plan carries, as a list")
set(KAEPORA_TESTED_NEW_ACTOR_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with the Megaton Hammer for sale through a goods actor it adds")
set(KAEPORA_TESTED_SKULLTULA_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with a Gold Skulltula that drops a token giving the Boomerang")
set(KAEPORA_TESTED_PICKUP_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with the Mirror Shield at the Lon Lon Ranch heart piece")
set(KAEPORA_TESTED_SONG_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with Impa teaching the Minuet and the windmill Epona's Song")
set(KAEPORA_TESTED_COLOSSUS_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with the Colossus's ways out waiting for the Spirit quest")
set(KAEPORA_TESTED_PROGRAM_MOD "" CACHE PATH "The folder of the mod tested in the game with the patch to its program")
set(KAEPORA_TESTED_BOMB_MOD "" CACHE PATH
    "The folder of the mod tested in the game with the bomb's lift grade at None")
set(KAEPORA_TESTED_MIDO_MOD "" CACHE PATH
    "The folder of the mod tested in the game with Mido's castle entry taken out of his schedule")
set(KAEPORA_TESTED_SHOP_GOODS_MOD "" CACHE PATH
    "The folder of the mod tested in the game with every item that can reach a shop's shelf on one")
set(KAEPORA_TESTED_PEDESTAL_MOD "" CACHE PATH
    "The folder of the mod tested in the game with the Master Sword pedestal open to every adult")
set(KAEPORA_TESTED_SHEIK_MOD "" CACHE PATH
    "The folder of the mod tested in the game with Sheik waiting behind the Master Sword pedestal")
set(KAEPORA_TESTED_OPENING_MOD "" CACHE PATH "The folder of the mod tested in the game with the opening skipped")
set(KAEPORA_TESTED_CUTSCENES_MOD "" CACHE PATH
    "The folder of the mod tested in the game with every skippable cutscene skipped")
set(KAEPORA_TESTED_STONE_TEXT_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with a text of ours on the Kokiri Forest gossip stone")
set(KAEPORA_TESTED_MESSAGE_TEXT_MOD "" CACHE PATH
    "The romfs folder of the mod tested in the game with Mido's lines, the owner's, the children's and the sign")
set(PATCH_TEST ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/patch_test.py)
if(KAEPORA_GAME_NUP)
    add_test(NAME patch-roundtrip COMMAND kaepora-patch-roundtrip "${KAEPORA_GAME_NUP}")
    add_test(NAME patch-flow-roundtrip COMMAND kaepora-patch-flow-roundtrip "${KAEPORA_GAME_NUP}")
    add_test(NAME patch-pack-roundtrip COMMAND kaepora-patch-pack-roundtrip "${KAEPORA_GAME_NUP}")
    add_test(NAME patch-text-roundtrip COMMAND kaepora-patch-text-roundtrip "${KAEPORA_GAME_NUP}")
    foreach(test larger-plan bad-input other-version size-table size-table-unchanged together start-values
                 start-items gift gift-size-table stone-texts message-texts shop shop-table new-actors shelf-placement
                 single-goods-sign bundle-goods-sign shop-scenes-whole gift-table reward-table dive-depth skulltulas
                 skulltula-table loose-items goods-and-copies loose-item-table weapons-bring-containers
                 weapon-copies event-numbers song-table
                 presence lichii-built-ins opening-skipped cutscenes-skipped sword-scene-left-out sword-scene-kept)
        add_test(NAME patch-${test} COMMAND ${PATCH_TEST} ${test} $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}")
    endforeach()
    add_test(NAME remake-tables
        COMMAND ${Python3_EXECUTABLE} -I -B ${PROJECT_SOURCE_DIR}/tests/remake_tables_test.py "${KAEPORA_GAME_NUP}")
    add_test(NAME patch-fixed-seed-plan
        COMMAND ${PATCH_TEST} seed-plan $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                ${PROJECT_SOURCE_DIR}/tests/fixtures/plan-6332265119.json)
    foreach(TEST IN ITEMS same_files_as_the_two_programs a_seed_with_shop_goods_shuffled_is_patched
                          a_seed_with_gifts_and_shop_goods_shuffled_is_patched
                          a_seed_with_gold_skulltulas_shuffled_is_patched
                          a_seed_with_loose_items_shuffled_is_patched a_seed_with_songs_shuffled_is_patched
                          a_seed_with_dungeon_rewards_shuffled_is_patched two_seeds_in_one_process
                          same_seed_twice_in_one_process existing_folder_is_refused new_folder_in_a_missing_folder)
        add_test(NAME make_seed_${TEST}
                 COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tests/make_seed_test.py
                     $<TARGET_FILE:kaepora-make-seed> $<TARGET_FILE:kaepora-seedgen> $<TARGET_FILE:kaepora-patch>
                     "${KAEPORA_GAME_NUP}" MakeSeedTest.test_${TEST})
    endforeach()
    if(Qt6Test_FOUND)
        add_test(NAME window COMMAND kaepora-window-test)
        set_tests_properties(window PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;KAEPORA_GAME_NUP=${KAEPORA_GAME_NUP};KAEPORA_MAKE_SEED=$<TARGET_FILE:kaepora-make-seed>")
    endif()
    if(KAEPORA_TESTED_MOD)
        add_test(NAME patch-tested-mod
            COMMAND ${PATCH_TEST} tested-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}" "${KAEPORA_TESTED_MOD}")
    endif()
    if(KAEPORA_TESTED_START_MOD)
        add_test(NAME patch-tested-start-mod
            COMMAND ${PATCH_TEST} tested-start-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_START_MOD}")
    endif()
    if(KAEPORA_TESTED_SUBSTITUTE_MOD)
        add_test(NAME patch-tested-substitute-mod
            COMMAND ${PATCH_TEST} tested-substitute-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_SUBSTITUTE_MOD}")
    endif()
    if(KAEPORA_TESTED_ADDED_ITEM_MOD)
        add_test(NAME patch-tested-added-item-mod
            COMMAND ${PATCH_TEST} tested-added-item-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_ADDED_ITEM_MOD}")
    endif()
    if(KAEPORA_TESTED_HAMMER_ROOM_MOD)
        add_test(NAME patch-tested-hammer-room-mod
            COMMAND ${PATCH_TEST} tested-hammer-room-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_HAMMER_ROOM_MOD}")
    endif()
    if(KAEPORA_TESTED_NEW_ACTOR_MOD)
        add_test(NAME patch-tested-new-actor-mod
            COMMAND ${PATCH_TEST} tested-new-actor-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_NEW_ACTOR_MOD}")
    endif()
    if(KAEPORA_TESTED_SKULLTULA_MOD)
        add_test(NAME patch-tested-skulltula-mod
            COMMAND ${PATCH_TEST} tested-skulltula-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_SKULLTULA_MOD}")
    endif()
    if(KAEPORA_TESTED_PICKUP_MOD)
        add_test(NAME patch-tested-pickup-mod
            COMMAND ${PATCH_TEST} tested-pickup-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_PICKUP_MOD}")
    endif()
    if(KAEPORA_TESTED_SONG_MOD)
        add_test(NAME patch-tested-song-mod
            COMMAND ${PATCH_TEST} tested-song-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_SONG_MOD}")
    endif()
    if(KAEPORA_TESTED_COLOSSUS_MOD)
        add_test(NAME patch-tested-colossus-mod
            COMMAND ${PATCH_TEST} tested-colossus-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_COLOSSUS_MOD}")
    endif()
    if(KAEPORA_TESTED_GIFT_MOD)
        add_test(NAME patch-tested-gift-mod
            COMMAND ${PATCH_TEST} tested-gift-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_GIFT_MOD}")
    endif()
    if(KAEPORA_TESTED_FIRST_GIFTS_MOD)
        add_test(NAME patch-tested-first-gifts-mod
            COMMAND ${PATCH_TEST} tested-first-gifts-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_FIRST_GIFTS_MOD}")
    endif()
    if(KAEPORA_TESTED_STONE_TEXT_MOD)
        add_test(NAME patch-tested-stone-text-mod
            COMMAND ${PATCH_TEST} tested-stone-text-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_STONE_TEXT_MOD}")
    endif()
    if(KAEPORA_TESTED_MESSAGE_TEXT_MOD)
        add_test(NAME patch-tested-message-text-mod
            COMMAND ${PATCH_TEST} tested-message-text-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_MESSAGE_TEXT_MOD}")
    endif()
    if(KAEPORA_TESTED_EVENT_TEXT_MODS)
        add_test(NAME patch-tested-event-text-mods
            COMMAND ${PATCH_TEST} tested-event-text-mods $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    ${PROJECT_SOURCE_DIR}/tests/fixtures/plan-6332265119.json ${KAEPORA_TESTED_EVENT_TEXT_MODS})
    endif()
    if(KAEPORA_TESTED_PROGRAM_MOD)
        add_test(NAME patch-tested-program-mod
            COMMAND ${PATCH_TEST} tested-program-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_PROGRAM_MOD}")
    endif()
    if(KAEPORA_TESTED_BOMB_MOD)
        add_test(NAME patch-tested-bomb-mod
            COMMAND ${PATCH_TEST} tested-bomb-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_BOMB_MOD}")
    endif()
    if(KAEPORA_TESTED_MIDO_MOD)
        add_test(NAME patch-tested-mido-mod
            COMMAND ${PATCH_TEST} tested-mido-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_MIDO_MOD}")
    endif()
    if(KAEPORA_TESTED_SHOP_GOODS_MOD)
        add_test(NAME patch-tested-shop-goods-mod
            COMMAND ${PATCH_TEST} tested-shop-goods-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_SHOP_GOODS_MOD}")
    endif()
    if(KAEPORA_TESTED_PEDESTAL_MOD)
        add_test(NAME patch-tested-pedestal-mod
            COMMAND ${PATCH_TEST} tested-pedestal-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    ${PROJECT_SOURCE_DIR}/tests/fixtures/plan-6332265119.json "${KAEPORA_TESTED_PEDESTAL_MOD}")
    endif()
    if(KAEPORA_TESTED_SHEIK_MOD)
        add_test(NAME patch-tested-sheik-mod
            COMMAND ${PATCH_TEST} tested-sheik-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    ${PROJECT_SOURCE_DIR}/tests/fixtures/plan-6332265119.json "${KAEPORA_TESTED_SHEIK_MOD}")
    endif()
    if(KAEPORA_TESTED_OPENING_MOD)
        add_test(NAME patch-tested-opening-mod
            COMMAND ${PATCH_TEST} tested-opening-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_OPENING_MOD}")
    endif()
    if(KAEPORA_TESTED_CUTSCENES_MOD)
        add_test(NAME patch-tested-cutscenes-mod
            COMMAND ${PATCH_TEST} tested-cutscenes-mod $<TARGET_FILE:kaepora-patch> "${KAEPORA_GAME_NUP}"
                    "${KAEPORA_TESTED_CUTSCENES_MOD}")
    endif()
endif()

# Windows finds a program's libraries beside it or on PATH, so the tests find Qt's on PATH.
if(WIN32 AND Qt6Test_FOUND)
    get_property(tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${tests} PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
