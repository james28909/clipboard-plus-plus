#include "RegexTransform.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

struct CodeDeleter {
    void operator()(pcre2_code* code) const {
        if (code) pcre2_code_free(code);
    }
};

using CodePtr = std::unique_ptr<pcre2_code, CodeDeleter>;

uint32_t CompileOptions(const RegexTransformDefinition& transform) {
    uint32_t options = PCRE2_UTF;
    if (!transform.caseSensitive) options |= PCRE2_CASELESS;
    if (transform.multiline) options |= PCRE2_MULTILINE;
    if (transform.dotMatchesNewline) options |= PCRE2_DOTALL;
    return options;
}

CodePtr Compile(const RegexTransformDefinition& transform, std::string& error) {
    int errorNumber = 0;
    PCRE2_SIZE errorOffset = 0;
    pcre2_code* code = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(transform.pattern.c_str()),
        PCRE2_ZERO_TERMINATED, CompileOptions(transform),
        &errorNumber, &errorOffset, nullptr);
    if (!code) {
        PCRE2_UCHAR message[256]{};
        pcre2_get_error_message(errorNumber, message, sizeof(message));
        error = "PCRE2 error at offset " +
            std::to_string(static_cast<size_t>(errorOffset)) + ": " +
            reinterpret_cast<const char*>(message);
        return {};
    }
    pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
    return CodePtr(code);
}

} // namespace

std::string ValidateRegexTransform(const RegexTransformDefinition& transform) {
    if (transform.name.empty()) return "Transform name is required.";
    if (transform.pattern.empty()) return "Pattern is required.";
    std::string error;
    Compile(transform, error);
    return error;
}

RegexTransformResult ApplyRegexTransform(
    const RegexTransformDefinition& transform, const std::string& input) {
    RegexTransformResult result;
    result.output = input;
    if (transform.pattern.empty()) {
        result.error = "Pattern is required.";
        return result;
    }

    std::string compileError;
    CodePtr code = Compile(transform, compileError);
    if (!code) {
        result.error = std::move(compileError);
        return result;
    }

    uint32_t options = PCRE2_SUBSTITUTE_EXTENDED |
                       PCRE2_SUBSTITUTE_UNSET_EMPTY |
                       PCRE2_SUBSTITUTE_OVERFLOW_LENGTH;
    if (transform.replaceAll) options |= PCRE2_SUBSTITUTE_GLOBAL;

    std::vector<PCRE2_UCHAR> output(std::max<size_t>(256, input.size() + 64));
    for (int attempt = 0; attempt < 3; ++attempt) {
        PCRE2_SIZE outputSize = output.size();
        const int replaced = pcre2_substitute(
            code.get(), reinterpret_cast<PCRE2_SPTR>(input.data()), input.size(),
            0, options, nullptr, nullptr,
            reinterpret_cast<PCRE2_SPTR>(transform.replacement.c_str()),
            transform.replacement.size(), output.data(), &outputSize);
        if (replaced == PCRE2_ERROR_NOMATCH) {
            result.ok = true;
            result.replacements = 0;
            return result;
        }
        if (replaced >= 0) {
            result.ok = true;
            result.replacements = replaced;
            result.output.assign(reinterpret_cast<const char*>(output.data()),
                                 static_cast<size_t>(outputSize));
            return result;
        }
        if (replaced == PCRE2_ERROR_NOMEMORY && outputSize > output.size()) {
            output.resize(static_cast<size_t>(outputSize) + 1);
            continue;
        }
        PCRE2_UCHAR message[256]{};
        pcre2_get_error_message(replaced, message, sizeof(message));
        result.error = "PCRE2 substitution error: " +
                       std::string(reinterpret_cast<const char*>(message));
        return result;
    }
    result.error = "PCRE2 substitution output was too large.";
    return result;
}
