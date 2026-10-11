#pragma once
#include "types.h"
#include <string>
#include <vector>

// A code fence alone is never an instruction to execute a Revit script.
void decodeAssistantResponse(GenerateResult &result);
bool isConversationOnly(const std::string &input);

class LLMClient {
public:
    virtual ~LLMClient() = default;
    virtual GenerateResult generate(const std::vector<ChatMessage>& messages) = 0;
    virtual void generate_stream(const std::vector<ChatMessage>& messages,
                                 SSEChunkCallback on_chunk) = 0;
};
