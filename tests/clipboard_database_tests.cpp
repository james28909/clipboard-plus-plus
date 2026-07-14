#include "../src/clipboard/ClipboardDatabase.h"
#include "../src/security/EncryptedSqliteVfs.h"
#include "../third_party/sqlite/sqlite3.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool value, const char* name) {
    if (value) return true;
    std::cerr << "FAILED: " << name << '\n';
    return false;
}

bool Contains(const std::filesystem::path& path, const std::string& needle) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::vector<char> bytes(std::istreambuf_iterator<char>(input), {});
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

ClipboardItem MakeItem(uint64_t id, const std::string& secret, bool pinned) {
    ClipboardItem item;
    item.id = id;
    item.type = ContentType::Text;
    item.text = secret;
    item.sourceProcess = "private-process.exe";
    item.tags = TAG_SECRET | TAG_CODE;
    item.pinned = pinned;
    item.timestamp = std::chrono::system_clock::now();
    item.createdAt = item.timestamp;
    item.updatedAt = item.timestamp;
    item.lastUsedAt = item.timestamp;
    item.formats = {
        ClipboardFormatRecord{13, "CF_UNICODETEXT", 0, 8,
                              ClipboardFormatStatus::Preserved, true,
                              {0x41, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00}},
        ClipboardFormatRecord{49152, "Shell IDList Array", 1, 128,
                              ClipboardFormatStatus::MetadataOnly, false, {}},
    };
    item.EnsureContentHash();
    return item;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--create-cli-fixture") {
        const std::filesystem::path appData = std::filesystem::u8path(argv[2]);
        const auto directory = appData / "Clipboard++";
        std::error_code fixtureError;
        std::filesystem::create_directories(directory, fixtureError);
        ClipboardDatabase database;
        std::string error;
        ClipboardProfileConfig profile{
            "default", "CLI Fixture", "created", "updated", ""};
        ClipboardHistory history(kMaxClipboardHistoryItems);
        history.LoadSnapshot({
            MakeItem(1, "CLI_HISTORY_ALPHA_EXACT_20260713", false),
            MakeItem(2, "CLI_HISTORY_SEARCH_NEEDLE_EXACT_20260713", false),
        }, 3);
        const ClipboardItem archived = MakeItem(
            7, "CLI_FIXTURE_EXACT_STORED_BYTES_20260713", false);
        const bool created = !fixtureError &&
            database.Open(directory / "clipboard.db", &error) &&
            database.UpsertProfile(profile, 0) &&
            database.SaveHistory(profile.id, history) &&
            database.SetActiveProfileId(profile.id) &&
            database.ArchiveItem(profile.id, archived);
        if (!created) {
            std::cerr << "Could not create CLI fixture: " << error << '\n';
            return 1;
        }
        return 0;
    }

    const auto directory = std::filesystem::temp_directory_path() /
        ("clipboardpp-database-tests-" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);
    const auto path = directory / "clipboard.db";
    const std::string secret = "clipboard-history-secret-20260713";
    const std::string vaultSecret = "searchable-vault-secret-20260713";
    const std::string slotSecret = "named-slot-secret-20260713";
    const std::string transformSecret = "regex-transform-secret-20260713";
    const std::string templateSecret = "paste-template-secret-20260713";
    const std::string actionSecret = "custom-action-secret-20260714";
    bool ok = true;

    {
        KeyBinding genericAlt;
        genericAlt.alt = true;
        genericAlt.vkey = 'K';
        ModifierState leftAltState{};
        leftAltState.leftAlt = true;
        ModifierState rightAltState{};
        rightAltState.rightAlt = true;
        ok &= Expect(genericAlt.Matches(leftAltState, 'K') &&
                     genericAlt.Matches(rightAltState, 'K'),
                     "generic Alt binding accepts either Alt key");
        KeyBinding leftAlt = genericAlt;
        leftAlt.altSide = ModifierSide::Left;
        ok &= Expect(leftAlt.Matches(leftAltState, 'K') &&
                     !leftAlt.Matches(rightAltState, 'K'),
                     "Left Alt binding rejects Right Alt");
        KeyBinding rightAlt = genericAlt;
        rightAlt.altSide = ModifierSide::Right;
        ok &= Expect(rightAlt.Matches(rightAltState, 'K') &&
                     !rightAlt.Matches(leftAltState, 'K'),
                     "Right Alt binding rejects Left Alt");

        ModifierState leftModifiers{};
        leftModifiers.leftCtrl = true;
        leftModifiers.leftShift = true;
        KeyBinding sided;
        sided.ctrl = true;
        sided.shift = true;
        sided.ctrlSide = ModifierSide::Left;
        sided.shiftSide = ModifierSide::Left;
        sided.vkey = 'J';
        ok &= Expect(sided.Matches(leftModifiers, 'J'),
                     "left Ctrl and Shift binding matches left modifiers");
        leftModifiers.leftCtrl = false;
        leftModifiers.rightCtrl = true;
        ok &= Expect(!sided.Matches(leftModifiers, 'J'),
                     "left Ctrl binding rejects Right Ctrl");
        ok &= Expect(!leftAlt.Overlaps(rightAlt) &&
                     genericAlt.Overlaps(leftAlt) && genericAlt.Overlaps(rightAlt),
                     "side-specific conflict overlap is detected correctly");

        KeyBinding dualCtrl;
        dualCtrl.vkey = '1';
        dualCtrl.exactModifiers = true;
        dualCtrl.physicalModifiers = static_cast<uint8_t>(
            ModifierState::LeftCtrlBit | ModifierState::RightCtrlBit);
        ModifierState bothCtrls{};
        bothCtrls.leftCtrl = true;
        bothCtrls.rightCtrl = true;
        ok &= Expect(dualCtrl.Matches(bothCtrls, '1'),
                     "exact chord accepts both Ctrl keys together");
        bothCtrls.rightCtrl = false;
        ok &= Expect(!dualCtrl.Matches(bothCtrls, '1'),
                     "exact dual-Ctrl chord rejects a single Ctrl key");

        SlotBankSettings bank;
        bank.numberKeys = true;
        bank.letterKeys = false;
        bank.functionKeys = true;
        ok &= Expect(HotkeyManager::BankSlotFromVKey(bank, '3') == 2 &&
                     HotkeyManager::BankSlotFromVKey(bank, 'A') == -1 &&
                     HotkeyManager::BankSlotFromVKey(bank, VK_F12) == 46,
                     "slot bank key groups map to stable slot positions");

        HotkeySettings routing;
        routing.globalHistoryBank.chord = dualCtrl;
        routing.globalHistoryBank.chord.vkey = 0;
        routing.pinnedHistoryBank.enabled = false;
        routing.profileBank.enabled = false;
        routing.popupHistoryBank.enabled = false;
        int routedSlot = -1;
        bothCtrls.rightCtrl = true;
        ok &= Expect(HotkeyManager::ResolveSlotBank(
                         routing, bothCtrls, '1', false, routedSlot) ==
                         HotkeyAction::PasteHistorySlot && routedSlot == 0,
                     "global history bank resolves while popup is hidden");
        routing.pinnedHistoryBank = routing.globalHistoryBank;
        ok &= Expect(HotkeyManager::ResolveSlotBank(
                         routing, bothCtrls, '2', true, routedSlot) ==
                         HotkeyAction::PasteHistorySlot && routedSlot == 1,
                     "higher-priority history bank wins an overlapping bank route");
        KeyBinding namedDouble = dualCtrl;
        namedDouble.action = HotkeyAction::PasteNamedSlot;
        namedDouble.data = 42;
        ok &= Expect(namedDouble.Matches(ModifierState::FromMask(
                         dualCtrl.physicalModifiers), '1') &&
                     HotkeyManager::ResolveSlotBank(
                         routing, ModifierState::FromMask(dualCtrl.physicalModifiers),
                         '1', false, routedSlot) == HotkeyAction::PasteHistorySlot,
                     "named-slot chord and hidden bank route can share a double-tap chord");
    }

    {
        ClipboardHistory history(10);
        ok &= Expect(history.Push(MakeItem(0, "deduplication test", false)),
                     "first duplicate-test item is inserted");
        ok &= Expect(!history.Push(MakeItem(0, "deduplication test", false)) &&
                     history.Size() == 1,
                     "deduplication consolidates repeated content by default");
        history.SetDeduplicationEnabled(false);
        ok &= Expect(history.Push(MakeItem(0, "deduplication test", false)) &&
                     history.Size() == 2,
                     "disabled deduplication keeps repeated content separately");
        history.SetDeduplicationEnabled(true);
        ok &= Expect(!history.Push(MakeItem(0, "deduplication test", false)) &&
                     history.Size() == 2,
                     "re-enabled deduplication refreshes one existing copy");

        ClipboardHistory bulkHistory(10);
        bulkHistory.LoadSnapshot({MakeItem(101, "bulk one", false),
                                  MakeItem(102, "bulk two", false)}, 103);
        int changeNotifications = 0;
        bulkHistory.SetChangedCallback([&]() { ++changeNotifications; });
        ok &= Expect(bulkHistory.MoveItemsById(
                         {101, 102}, ClipboardHistory::MoveTarget::None) &&
                     changeNotifications == 1,
                     "bulk paste-use update persists selected items once");
    }

    ClipboardProfileConfig first{"default", "Private Default", "created", "updated", ""};
    ClipboardProfileConfig second{"work", "Confidential Work", "created2", "updated2", "work.exe"};
    ClipboardHistory saved;
    saved.LoadSnapshot({MakeItem(11, secret, true), MakeItem(12, "regular secret", false)}, 42);

    {
        ClipboardDatabase database;
        std::string error;
        ok &= Expect(database.Open(path, &error), "encrypted clipboard database opens");
        ok &= Expect(database.Begin(), "migration transaction begins");
        ok &= Expect(database.UpsertProfile(first, 0), "first profile is stored");
        ok &= Expect(database.UpsertProfile(second, 1), "second profile is stored");
        ok &= Expect(database.SaveHistory(first.id, saved), "history is stored");
        ClipboardItem archived = MakeItem(77, vaultSecret, false);
        ok &= Expect(database.ArchiveItem(first.id, archived), "overflow item is archived");
        ok &= Expect(database.ArchiveItem(first.id, archived), "duplicate archive refreshes");
        NamedClipboardSlot slot;
        slot.name = "Release signature";
        slot.text = slotSecret;
        ok &= Expect(database.SaveNamedSlot(slot) && slot.slotId > 0,
                     "named slot is stored");
        RegexTransformDefinition transform;
        transform.name = "Normalize secret";
        transform.pattern = "(regex)-(transform)-(secret)-([0-9]+)";
        transform.replacement = transformSecret + "-$4";
        transform.caseSensitive = false;
        transform.multiline = true;
        transform.dotMatchesNewline = true;
        transform.replaceAll = false;
        ok &= Expect(database.SaveRegexTransform(transform) &&
                     transform.transformId > 0,
                     "regex transform is stored");
        PasteTemplateDefinition pasteTemplate;
        pasteTemplate.name = "Encrypted contact template";
        pasteTemplate.body = templateSecret + ": {{1}} / {{slot:Email}}";
        ok &= Expect(database.SavePasteTemplate(pasteTemplate) &&
                     pasteTemplate.templateId > 0,
                     "paste template is stored");
        CustomActionDefinition customAction;
        customAction.label = "Encrypted workflow";
        customAction.icon = ">";
        customAction.toolbarOrder = 4;
        customAction.placement = CustomActionPlacement::Destinations;
        customAction.input = CustomActionInput::OrderedSelection;
        customAction.output = CustomActionOutput::LaunchExecutable;
        customAction.outputValue = R"(C:\Tools\workflow.exe)";
        customAction.executableArguments = "--secret " + actionSecret + " --text {{text}}";
        customAction.confirmation = CustomActionConfirmation::Always;
        customAction.hotkeyEnabled = true;
        customAction.hotkey.exactModifiers = true;
        customAction.hotkey.physicalModifiers = ModifierState::LeftCtrlBit;
        customAction.hotkey.vkey = 'G';
        CustomActionStep customTemplate;
        customTemplate.type = CustomActionStepType::Template;
        customTemplate.value = actionSecret + ": {{1}}";
        customAction.steps.push_back(customTemplate);
        ok &= Expect(database.SaveCustomAction(customAction) &&
                     customAction.actionId > 0,
                     "custom action is stored");
        ok &= Expect(database.SetActiveProfileId(second.id), "active profile is stored");
        ok &= Expect(database.IntegrityCheck(), "database passes integrity check");
        ok &= Expect(database.Commit(), "migration transaction commits");
    }

    ok &= Expect(EncryptedSqliteVfs::HasKey(path), "DPAPI key sidecar exists");
    ok &= Expect(!Contains(path, "SQLite format 3"), "database header is encrypted");
    ok &= Expect(!Contains(path, secret), "main database hides clipboard text");
    ok &= Expect(!Contains(path, vaultSecret), "main database hides vault text");
    ok &= Expect(!Contains(path, slotSecret), "main database hides named slot text");
    ok &= Expect(!Contains(path, transformSecret),
                 "main database hides regex transform definitions");
    ok &= Expect(!Contains(path, templateSecret),
                 "main database hides paste template definitions");
    ok &= Expect(!Contains(path, actionSecret),
                 "main database hides custom action bodies and arguments");
    ok &= Expect(!Contains(path, "Confidential Work"), "main database hides profile names");
    const std::filesystem::path wal = path.u8string() + "-wal";
    if (std::filesystem::exists(wal)) {
        ok &= Expect(!Contains(wal, secret), "WAL hides clipboard text");
        ok &= Expect(!Contains(wal, "Confidential Work"), "WAL hides profile names");
    }

    {
        ClipboardDatabase database;
        std::string error;
        ok &= Expect(database.Open(path, &error), "encrypted clipboard database reopens");
        std::vector<ClipboardProfileConfig> profiles;
        ok &= Expect(database.LoadProfiles(profiles) && profiles.size() == 2,
                     "all profiles round-trip");
        ok &= Expect(profiles.size() == 2 && profiles[1].name == second.name &&
                     profiles[1].processName == second.processName,
                     "profile metadata round-trips");
        std::string active;
        ok &= Expect(database.GetActiveProfileId(active) && active == second.id,
                     "active profile round-trips");
        ClipboardHistory loaded;
        bool found = false;
        ok &= Expect(database.LoadHistory(first.id, loaded, found) && found,
                     "history reloads");
        const auto items = loaded.Snapshot();
        ok &= Expect(items.size() == 2 && items[0].text == secret && items[0].pinned,
                     "history contents and pin state round-trip");
        ok &= Expect(items.size() == 2 && items[0].formats.size() == 2 &&
                     items[0].formats[0].name == "CF_UNICODETEXT" &&
                     items[0].formats[0].data ==
                         std::vector<uint8_t>({0x41, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00}) &&
                     items[0].formats[1].status == ClipboardFormatStatus::MetadataOnly &&
                     items[0].formats[1].data.empty(),
                     "format manifest and exact bytes round-trip");
        ok &= Expect(loaded.NextId() == 42, "next history ID round-trips");
        std::vector<NamedClipboardSlot> slots;
        ok &= Expect(database.LoadNamedSlots(slots) && slots.size() == 1 &&
                     slots[0].name == "Release signature" &&
                     slots[0].text == slotSecret,
                     "named slot round-trips");
        if (!slots.empty()) {
            slots[0].name = "Updated signature";
            slots[0].text = "updated named slot text";
            ok &= Expect(database.SaveNamedSlot(slots[0]), "named slot updates");
            NamedClipboardSlot duplicate;
            duplicate.name = "UPDATED SIGNATURE";
            duplicate.text = "duplicate";
            ok &= Expect(!database.SaveNamedSlot(duplicate),
                         "named slot names are unique without case sensitivity");
            std::vector<NamedClipboardSlot> updatedSlots;
            ok &= Expect(database.LoadNamedSlots(updatedSlots) &&
                         updatedSlots.size() == 1 &&
                         updatedSlots[0].text == "updated named slot text",
                         "named slot update round-trips");
        }
        std::vector<RegexTransformDefinition> transforms;
        ok &= Expect(database.LoadRegexTransforms(transforms) && transforms.size() == 1 &&
                     transforms[0].name == "Normalize secret" &&
                     transforms[0].replacement == transformSecret + "-$4" &&
                     !transforms[0].caseSensitive && transforms[0].multiline &&
                     transforms[0].dotMatchesNewline && !transforms[0].replaceAll,
                     "regex transform round-trips");
        if (!transforms.empty()) {
            transforms[0].replacement = "updated replacement";
            transforms[0].replaceAll = true;
            ok &= Expect(database.SaveRegexTransform(transforms[0]),
                         "regex transform updates");
            RegexTransformDefinition duplicate;
            duplicate.name = "NORMALIZE SECRET";
            duplicate.pattern = ".+";
            duplicate.replacement = "duplicate";
            ok &= Expect(!database.SaveRegexTransform(duplicate),
                         "regex transform names are unique without case sensitivity");
            std::vector<RegexTransformDefinition> updatedTransforms;
            ok &= Expect(database.LoadRegexTransforms(updatedTransforms) &&
                         updatedTransforms.size() == 1 &&
                         updatedTransforms[0].replacement == "updated replacement" &&
                         updatedTransforms[0].replaceAll,
                         "regex transform update round-trips");
        }
        std::vector<PasteTemplateDefinition> pasteTemplates;
        ok &= Expect(database.LoadPasteTemplates(pasteTemplates) &&
                     pasteTemplates.size() == 1 &&
                     pasteTemplates[0].body == templateSecret +
                         ": {{1}} / {{slot:Email}}",
                     "paste template round-trips");
        if (!pasteTemplates.empty()) {
            pasteTemplates[0].body = "Updated {{1}}";
            ok &= Expect(database.SavePasteTemplate(pasteTemplates[0]),
                         "paste template updates");
            PasteTemplateDefinition duplicateTemplate;
            duplicateTemplate.name = "ENCRYPTED CONTACT TEMPLATE";
            duplicateTemplate.body = "{{1}}";
            ok &= Expect(!database.SavePasteTemplate(duplicateTemplate),
                         "paste template names are unique without case sensitivity");
        }
        std::vector<CustomActionDefinition> customActions;
        ok &= Expect(database.LoadCustomActions(customActions) &&
                     customActions.size() == 1 &&
                     customActions[0].label == "Encrypted workflow" &&
                     customActions[0].steps.size() == 1 &&
                     customActions[0].steps[0].value == actionSecret + ": {{1}}" &&
                     customActions[0].executableArguments.find(actionSecret) !=
                         std::string::npos &&
                     customActions[0].hotkeyEnabled &&
                     customActions[0].hotkey.vkey == 'G',
                     "custom action payload and hotkey round-trip");
        if (!customActions.empty()) {
            customActions[0].label = "Updated workflow";
            customActions[0].toolbarOrder = 2;
            customActions[0].enabled = false;
            ok &= Expect(database.SaveCustomAction(customActions[0]),
                         "custom action updates");
            CustomActionDefinition duplicateAction = customActions[0];
            duplicateAction.actionId = 0;
            duplicateAction.label = "UPDATED WORKFLOW";
            ok &= Expect(!database.SaveCustomAction(duplicateAction),
                         "custom action labels are unique without case sensitivity");
        }
        size_t vaultCount = 0;
        ok &= Expect(database.VaultCount(first.id, vaultCount) && vaultCount == 1,
                     "vault deduplicates by content hash");
        std::vector<ClipboardVaultEntry> vaultEntries;
        ok &= Expect(database.SearchVault(first.id, "vault-secret", vaultEntries) &&
                     vaultEntries.size() == 1 && vaultEntries[0].item.text == vaultSecret,
                     "vault search returns matching archived content");
        ok &= Expect(database.DeleteVaultItem(first.id, vaultEntries[0].archiveId),
                     "vault item deletes");
        ok &= Expect(database.VaultCount(first.id, vaultCount) && vaultCount == 0,
                     "vault delete is persisted");

        ClipboardHistory limited(2);
        limited.SetOverflowCallback([&](ClipboardItem item) {
            database.ArchiveItem(first.id, item);
        });
        limited.Push(MakeItem(0, "overflow oldest", false));
        limited.Push(MakeItem(0, "overflow middle", false));
        limited.Push(MakeItem(0, "overflow newest", false));
        ok &= Expect(limited.Size() == 2, "active history enforces its item limit");
        ok &= Expect(database.VaultCount(first.id, vaultCount) && vaultCount == 1,
                     "oldest overflow item moves into the vault");
        vaultEntries.clear();
        ok &= Expect(database.SearchVault(first.id, "overflow oldest", vaultEntries) &&
                     vaultEntries.size() == 1,
                     "overflowed item is searchable");
        ClipboardVaultEntry restored;
        ok &= Expect(database.GetVaultItem(first.id, vaultEntries[0].archiveId, restored),
                     "archived item can be loaded for restore");
        restored.item.id = 0;
        limited.Push(restored.item);
        ok &= Expect(database.DeleteVaultItem(first.id, restored.archiveId),
                     "restored archive record is removed");
        ok &= Expect(limited.Size() == 2 &&
                     database.VaultCount(first.id, vaultCount) && vaultCount == 1,
                     "restoring into a full history archives the displaced item");
        ok &= Expect(database.PruneVault(first.id, 1) &&
                     database.VaultCount(first.id, vaultCount) && vaultCount == 0,
                     "vault byte limit prunes the oldest payloads");
        ok &= Expect(database.ArchiveItem(first.id,
                     MakeItem(0, "cascade archive", false)),
                     "vault item exists before profile deletion");
        ok &= Expect(database.DeleteProfile(first.id), "profile deletes");
        found = true;
        ClipboardHistory deleted;
        ok &= Expect(database.LoadHistory(first.id, deleted, found) && !found,
                     "profile deletion cascades to history");
        ok &= Expect(database.VaultCount(first.id, vaultCount) && vaultCount == 0,
                     "profile deletion cascades to vault items");
        slots.clear();
        ok &= Expect(database.LoadNamedSlots(slots) && slots.size() == 1,
                     "named slots are independent of clipboard profiles");
        if (!slots.empty())
            ok &= Expect(database.DeleteNamedSlot(slots[0].slotId),
                         "named slot deletes");
        slots.clear();
        ok &= Expect(database.LoadNamedSlots(slots) && slots.empty(),
                     "named slot deletion persists");
        transforms.clear();
        ok &= Expect(database.LoadRegexTransforms(transforms) && transforms.size() == 1,
                     "regex transforms are independent of clipboard profiles");
        if (!transforms.empty())
            ok &= Expect(database.DeleteRegexTransform(transforms[0].transformId),
                         "regex transform deletes");
        transforms.clear();
        ok &= Expect(database.LoadRegexTransforms(transforms) && transforms.empty(),
                     "regex transform deletion persists");
        pasteTemplates.clear();
        ok &= Expect(database.LoadPasteTemplates(pasteTemplates) &&
                     pasteTemplates.size() == 1 &&
                     pasteTemplates[0].body == "Updated {{1}}",
                     "paste templates are independent of clipboard profiles");
        if (!pasteTemplates.empty())
            ok &= Expect(database.DeletePasteTemplate(pasteTemplates[0].templateId),
                         "paste template deletes");
        pasteTemplates.clear();
        ok &= Expect(database.LoadPasteTemplates(pasteTemplates) && pasteTemplates.empty(),
                     "paste template deletion persists");
        customActions.clear();
        ok &= Expect(database.LoadCustomActions(customActions) &&
                     customActions.size() == 1 &&
                     customActions[0].label == "Updated workflow" &&
                     !customActions[0].enabled &&
                     customActions[0].toolbarOrder == 2,
                     "custom actions are independent of clipboard profiles");
        if (!customActions.empty())
            ok &= Expect(database.DeleteCustomAction(customActions[0].actionId),
                         "custom action deletes");
        customActions.clear();
        ok &= Expect(database.LoadCustomActions(customActions) && customActions.empty(),
                     "custom action deletion persists");
    }

    sqlite3* ordinary = nullptr;
    sqlite3_open_v2(path.u8string().c_str(), &ordinary, SQLITE_OPEN_READONLY, nullptr);
    sqlite3_stmt* statement = nullptr;
    const bool rejected = !ordinary || sqlite3_prepare_v2(
        ordinary, "SELECT * FROM profiles;", -1, &statement, nullptr) != SQLITE_OK;
    ok &= Expect(rejected, "ordinary SQLite cannot read clipboard.db");
    sqlite3_finalize(statement);
    if (ordinary) sqlite3_close(ordinary);

    std::filesystem::remove_all(directory, ec);
    if (!ok) return 1;
    std::cout << "clipboard database tests passed\n";
    return 0;
}
