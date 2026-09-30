#include "settings.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <objbase.h>
#include <xmllite.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{

struct KeyName
{
    const char *name;
    unsigned int vk;
};

const KeyName g_keyNames[] = {
    {"BACKSPACE", 0x08}, {"TAB", 0x09},     {"ENTER", 0x0D},   {"RETURN", 0x0D},
    {"ESCAPE", 0x1B},    {"ESC", 0x1B},     {"SPACE", 0x20},   {"PAGEUP", 0x21},
    {"PAGEDOWN", 0x22},  {"END", 0x23},     {"HOME", 0x24},    {"LEFT", 0x25},
    {"UP", 0x26},        {"RIGHT", 0x27},   {"DOWN", 0x28},    {"INSERT", 0x2D},
    {"DELETE", 0x2E},    {"DEL", 0x2E},     {"NUMPAD0", 0x60}, {"NUMPAD1", 0x61},
    {"NUMPAD2", 0x62},   {"NUMPAD3", 0x63}, {"NUMPAD4", 0x64}, {"NUMPAD5", 0x65},
    {"NUMPAD6", 0x66},   {"NUMPAD7", 0x67}, {"NUMPAD8", 0x68}, {"NUMPAD9", 0x69},
};

std::string upper(const std::string &text)
{
    std::string result = text;
    for (char &c : result)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
}

std::string lower(const std::string &text)
{
    std::string result = text;
    for (char &c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

std::string trim(const std::string &text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        begin++;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        end--;
    return text.substr(begin, end - begin);
}

std::string narrow(const std::wstring &wide)
{
    std::string result;
    result.reserve(wide.size());
    for (wchar_t c : wide)
    {
        if (c < 128)
            result.push_back(static_cast<char>(c));
    }
    return result;
}

std::wstring widen(const std::string &text)
{
    return std::wstring(text.begin(), text.end());
}

bool keyNameToVk(const std::string &name, unsigned int &vk)
{
    std::string token = upper(trim(name));
    if (token.size() == 1)
    {
        char c = token[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        {
            vk = static_cast<unsigned int>(c);
            return true;
        }
    }
    if (token.size() >= 2 && token[0] == 'F')
    {
        int number = std::atoi(token.c_str() + 1);
        if (number >= 1 && number <= 24)
        {
            vk = static_cast<unsigned int>(0x70 + number - 1);
            return true;
        }
    }
    for (const KeyName &entry : g_keyNames)
    {
        if (token == entry.name)
        {
            vk = entry.vk;
            return true;
        }
    }
    return false;
}

std::string vkToKeyName(unsigned int vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9')
        return std::string(1, static_cast<char>(vk));
    if (vk >= 0x70 && vk <= 0x87)
        return "F" + std::to_string(vk - 0x70 + 1);
    for (const KeyName &entry : g_keyNames)
    {
        if (entry.vk == vk && std::string(entry.name) != "RETURN" &&
            std::string(entry.name) != "ESC" && std::string(entry.name) != "DEL" &&
            std::string(entry.name).rfind("NUMPAD", 0) != 0)
            return entry.name;
    }
    return "VK_" + std::to_string(vk);
}

std::vector<std::string> split(const std::string &text, char delimiter)
{
    std::vector<std::string> parts;
    std::string current;
    std::istringstream stream(text);
    while (std::getline(stream, current, delimiter))
        parts.push_back(current);
    if (!text.empty() && text.back() == delimiter)
        parts.push_back("");
    return parts;
}

bool readFileBytes(const std::string &path, std::string &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream stream;
    stream << in.rdbuf();
    out = stream.str();
    return true;
}

IStream *streamFromBytes(const std::string &bytes)
{
    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        return nullptr;
    if (!bytes.empty())
        stream->Write(bytes.data(), static_cast<ULONG>(bytes.size()), nullptr);

    LARGE_INTEGER zero{};
    zero.QuadPart = 0;
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    return stream;
}

std::wstring readElementText(IXmlReader *reader)
{
    std::wstring text;
    XmlNodeType type;
    while (reader->Read(&type) == S_OK)
    {
        if (type == XmlNodeType_EndElement)
            break;
        if (type == XmlNodeType_Text || type == XmlNodeType_CDATA)
        {
            const wchar_t *value = nullptr;
            if (SUCCEEDED(reader->GetValue(&value, nullptr)) && value)
                text += value;
        }
    }
    return text;
}

void applyTextSetting(const std::wstring &name, const std::wstring &value, Settings &settings)
{
    std::string key = lower(narrow(name));
    std::string text = lower(narrow(value));
    if (key == "preset")
        settings.preset = (text == "aoe2") ? Preset::AoE2 : Preset::Generic;
    else if (key == "eapm")
        settings.eapm = (text == "true" || text == "1");
    else if (key == "overlay_metric")
        settings.overlayEapm = (text == "eapm");
    else if (key == "hotkey")
        parseHotkey(text, settings.hotkey);
    else if (key == "replay_hotkey")
        parseHotkey(text, settings.replayHotkey);
    else if (key == "rec_analysis")
        settings.recAnalysis = (text == "true" || text == "1");
    else if (key == "rec_folder")
        settings.recFolder = narrow(value);
    else if (key == "eapm_dedup_ms")
        settings.eapmDedupMs = std::atoi(text.c_str());
    else if (key == "eapm_consecutive")
        settings.eapmConsecutive = (text == "true" || text == "1");
    else if (key == "eapm_ignore_game")
        settings.eapmIgnoreGame = (text == "true" || text == "1");
    else if (key == "live_eapm_debounce_ms")
        settings.liveEapmDebounceMs = std::atoi(text.c_str());
}

void parseAttributes(IXmlReader *reader, const std::string &element, Settings &settings)
{
    if (reader->MoveToFirstAttribute() != S_OK)
        return;

    do
    {
        const wchar_t *name = nullptr;
        const wchar_t *value = nullptr;
        if (SUCCEEDED(reader->GetLocalName(&name, nullptr)) &&
            SUCCEEDED(reader->GetValue(&value, nullptr)) && name && value)
        {
            std::string key = lower(narrow(name));
            std::string text = lower(narrow(value));
            int number = std::atoi(text.c_str());

            if (element == "overlay")
            {
                if (key == "visible")
                    settings.overlayVisible = (text == "true" || text == "1");
                else if (key == "x")
                    settings.overlayX = number;
                else if (key == "y")
                    settings.overlayY = number;
            }
            else if (element == "graph")
            {
                if (key == "x")
                    settings.graphX = number;
                else if (key == "y")
                    settings.graphY = number;
                else if (key == "width")
                    settings.graphWidth = number;
                else if (key == "height")
                    settings.graphHeight = number;
            }
        }
    } while (reader->MoveToNextAttribute() == S_OK);

    reader->MoveToElement();
}

void writeElement(IXmlWriter *writer, const wchar_t *name, const std::wstring &value)
{
    writer->WriteStartElement(nullptr, name, nullptr);
    writer->WriteString(value.c_str());
    writer->WriteEndElement();
}

void writeAttribute(IXmlWriter *writer, const wchar_t *name, const std::wstring &value)
{
    writer->WriteAttributeString(nullptr, name, nullptr, value.c_str());
}

std::wstring toWide(long long value)
{
    return widen(std::to_string(value));
}

} // namespace

bool parseHotkey(const std::string &text, Hotkey &hotkey)
{
    std::vector<std::string> parts = split(text, '+');
    if (parts.empty())
        return false;

    unsigned int modifiers = 0;
    unsigned int vk = 0;
    bool haveKey = false;

    for (size_t i = 0; i < parts.size(); i++)
    {
        std::string token = upper(trim(parts[i]));
        if (token.empty())
            return false;

        bool isLast = (i + 1 == parts.size());
        if (!isLast)
        {
            if (token == "CTRL" || token == "CONTROL")
                modifiers |= HotkeyModControl;
            else if (token == "ALT")
                modifiers |= HotkeyModAlt;
            else if (token == "SHIFT")
                modifiers |= HotkeyModShift;
            else if (token == "WIN" || token == "SUPER" || token == "META")
                modifiers |= HotkeyModWin;
            else
                return false;
        }
        else
        {
            haveKey = keyNameToVk(token, vk);
        }
    }

    if (!haveKey)
        return false;

    hotkey.modifiers = modifiers;
    hotkey.vk = vk;
    return true;
}

std::string formatHotkey(const Hotkey &hotkey)
{
    std::string result;
    if (hotkey.modifiers & HotkeyModControl)
        result += "Ctrl+";
    if (hotkey.modifiers & HotkeyModAlt)
        result += "Alt+";
    if (hotkey.modifiers & HotkeyModShift)
        result += "Shift+";
    if (hotkey.modifiers & HotkeyModWin)
        result += "Win+";
    result += vkToKeyName(hotkey.vk);
    return result;
}

bool loadSettings(const std::string &path, Settings &settings)
{
    std::string bytes;
    if (!readFileBytes(path, bytes))
        return false;

    IStream *stream = streamFromBytes(bytes);
    if (!stream)
        return false;

    IXmlReader *reader = nullptr;
    if (FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void **>(&reader), nullptr)))
    {
        stream->Release();
        return false;
    }

    if (FAILED(reader->SetInput(stream)))
    {
        reader->Release();
        stream->Release();
        return false;
    }

    XmlNodeType type;
    while (reader->Read(&type) == S_OK)
    {
        if (type != XmlNodeType_Element)
            continue;

        const wchar_t *local = nullptr;
        if (FAILED(reader->GetLocalName(&local, nullptr)) || !local)
            continue;

        std::wstring name = local;
        std::string elementName = lower(narrow(name));
        if (elementName == "overlay" || elementName == "graph")
        {
            parseAttributes(reader, elementName, settings);
        }
        else if (elementName == "preset" || elementName == "eapm" ||
                 elementName == "overlay_metric" || elementName == "hotkey" ||
                 elementName == "replay_hotkey" || elementName == "rec_analysis" ||
                 elementName == "rec_folder" || elementName == "eapm_dedup_ms" ||
                 elementName == "eapm_consecutive" || elementName == "eapm_ignore_game" ||
                 elementName == "live_eapm_debounce_ms")
        {
            std::wstring value = readElementText(reader);
            applyTextSetting(name, value, settings);
        }
    }

    reader->Release();
    stream->Release();
    return true;
}

bool saveSettings(const std::string &path, const Settings &settings)
{
    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)))
        return false;

    IXmlWriter *writer = nullptr;
    if (FAILED(CreateXmlWriter(__uuidof(IXmlWriter), reinterpret_cast<void **>(&writer), nullptr)))
    {
        stream->Release();
        return false;
    }

    writer->SetOutput(stream);
    writer->SetProperty(XmlWriterProperty_Indent, TRUE);
    writer->WriteStartDocument(XmlStandalone_Omit);
    writer->WriteStartElement(nullptr, L"settings", nullptr);

    writeElement(writer, L"preset", settings.preset == Preset::AoE2 ? L"aoe2" : L"generic");
    writeElement(writer, L"eapm", settings.eapm ? L"true" : L"false");
    writeElement(writer, L"hotkey", widen(formatHotkey(settings.hotkey)));
    writeElement(writer, L"overlay_metric", settings.overlayEapm ? L"eapm" : L"apm");
    writeElement(writer, L"replay_hotkey", widen(formatHotkey(settings.replayHotkey)));
    writeElement(writer, L"rec_analysis", settings.recAnalysis ? L"true" : L"false");
    writeElement(writer, L"rec_folder", widen(settings.recFolder));
    writeElement(writer, L"eapm_dedup_ms", toWide(settings.eapmDedupMs));
    writeElement(writer, L"eapm_consecutive", settings.eapmConsecutive ? L"true" : L"false");
    writeElement(writer, L"eapm_ignore_game", settings.eapmIgnoreGame ? L"true" : L"false");
    writeElement(writer, L"live_eapm_debounce_ms", toWide(settings.liveEapmDebounceMs));

    writer->WriteStartElement(nullptr, L"overlay", nullptr);
    writeAttribute(writer, L"visible", settings.overlayVisible ? L"true" : L"false");
    writeAttribute(writer, L"x", toWide(settings.overlayX));
    writeAttribute(writer, L"y", toWide(settings.overlayY));
    writer->WriteEndElement();

    writer->WriteStartElement(nullptr, L"graph", nullptr);
    writeAttribute(writer, L"x", toWide(settings.graphX));
    writeAttribute(writer, L"y", toWide(settings.graphY));
    writeAttribute(writer, L"width", toWide(settings.graphWidth));
    writeAttribute(writer, L"height", toWide(settings.graphHeight));
    writer->WriteEndElement();

    writer->WriteEndElement();
    writer->Flush();
    writer->Release();

    std::string bytes;
    LARGE_INTEGER zero{};
    zero.QuadPart = 0;
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);

    STATSTG stat{};
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)))
    {
        ULONG length = static_cast<ULONG>(stat.cbSize.QuadPart);
        bytes.resize(length);
        ULONG read = 0;
        if (length > 0)
            stream->Read(&bytes[0], length, &read);
        bytes.resize(read);
    }
    stream->Release();

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}
