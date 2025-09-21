
#include "TextCmdResource.hpp"

#include "../config/ConfigManager.hpp"
#include "../helpers/MiscFunctions.hpp"
#include <hyprgraphics/resource/resources/TextResource.hpp>

using namespace Hyprgraphics;

CTextCmdResource::CTextCmdResource(CTextResource::STextResourceData&& data) : m_data(std::move(data)) {
    ;
}

void CTextCmdResource::render() {
    static const auto                TRIM = g_pConfigManager->getValue<Hyprlang::INT>("general:text_trim");

    CTextResource::STextResourceData textData = m_data;

    textData.text = spawnSync(m_data.text);

    if (*TRIM) {
        textData.text.erase(0, textData.text.find_first_not_of(" \n\r\t"));
        textData.text.erase(textData.text.find_last_not_of(" \n\r\t") + 1);
    }

    Hyprgraphics::CTextResource textResource(std::move(textData));

    textResource.render();

    std::swap(m_asset, textResource.m_asset);
}
