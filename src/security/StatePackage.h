#pragma once

#include "../app/ConfigStore.h"
#include "../clipboard/ClipboardDatabase.h"
#include "../transforms/RegexTransform.h"
#include "../templates/PasteTemplate.h"
#include "../actions/CustomAction.h"

#include <filesystem>
#include <string>
#include <vector>

namespace state_package {

enum class ConflictPolicy { Skip, Replace, KeepBoth };

struct Data {
    std::string configurationJson;
    std::vector<ClipboardProfileConfig> profiles;
    std::vector<NamedClipboardSlot> namedSlots;
    std::vector<RegexTransformDefinition> transforms;
    std::vector<PasteTemplateDefinition> templates;
    std::vector<CustomActionDefinition> actions;
};

struct Result {
    bool ok{false};
    bool encrypted{false};
    std::string message;
};

Result Write(const std::filesystem::path& path, const Data& data,
             bool encrypted);
Result Read(const std::filesystem::path& path, Data& data);

bool StageConfigurationImport(const std::filesystem::path& dataDirectory,
                              const std::string& configurationJson,
                              std::string* error = nullptr);
bool ApplyPendingConfigurationImport(const std::filesystem::path& dataDirectory,
                                     std::string* error = nullptr);
bool HasPendingConfigurationImport(const std::filesystem::path& dataDirectory);

} // namespace state_package
