#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

// Minimal boundary stubs; scan decisions below come from src/nfc.cpp.
constexpr int HEX = 16, NFC_READ_SUCCESS = 1;
constexpr int DISPLAY_PRIORITY_WARNING = 1, DISPLAY_PRIORITY_ACTION = 2;
constexpr int STR_LOCATION = 1, STR_SCAN_SPOOL_FIRST = 2, STR_LOCATION_SET = 3;
enum { STR_SPOOL_TAG = 4, STR_WEIGHING = 5, STR_FAILURE = 6, STR_UNKNOWN_TAG = 7 };
enum { STR_KNOWN_SPOOL = 8, STR_QUICK_MODE = 9 };
struct String {
    std::string value;
    String(const char* text = "") : value(text) {}
    String(unsigned number, int) {
        do {
            value.insert(value.begin(), "0123456789abcdef"[number % 16]);
            number /= 16;
        } while (number);
    }
    size_t length() const { return value.length(); }
    int toInt() const { return value.empty() ? 0 : std::stoi(value); }
    void toUpperCase() { std::transform(value.begin(), value.end(), value.begin(), ::toupper); }
    String& operator+=(const String& other) { value += other.value; return *this; }
    String& operator+=(char character) { value += character; return *this; }
    int indexOf(const String& needle, int start = 0) const {
        auto found = value.find(needle.value, start);
        return found == std::string::npos ? -1 : static_cast<int>(found);
    }
    String substring(int start, int end) const { return value.substr(start, end - start).c_str(); }
    String operator+(const String& other) const { String result = *this; result += other; return result; }
    const char* c_str() const { return value.c_str(); }
    bool operator==(const char* other) const { return value == other; }
    bool operator!=(const char* other) const { return value != other; }
};
String operator+(const char* first, const String& second) { return String(first) + second; }
struct SerialStub {
    template<class... T> void println(T...) {}
    template<class... T> void print(T...) {}
    template<class... T> void printf(T...) {}
} Serial;
struct JsonValue {
    std::string text;
    bool numeric = false;
    JsonValue& operator=(const char* value) { text = value; numeric = false; return *this; }
    JsonValue& operator=(int value) { text = std::to_string(value); numeric = true; return *this; }
    template<class T> bool is() const {
        if constexpr (std::is_same_v<T, String>) return !numeric && !text.empty();
        else return numeric;
    }
    template<class T> T as() const {
        if constexpr (std::is_same_v<T, String>) return String(text.c_str());
        else return text.empty() ? 0 : std::stoi(text);
    }
    bool operator!=(const char* value) const { return text != value; }
    const char* operator|(const char* fallback) const { return text.empty() ? fallback : text.c_str(); }
};
struct JsonDocument {
    std::map<std::string, JsonValue> values;
    JsonValue& operator[](String key) { return values[key.value]; }
} doc;
void serializeJson(JsonDocument&, String& output) { output = "{}"; }
bool scanRequestActive = false;
bool filamanConnected = true;
bool nfcWriteInProgress = false, failDecode = false;
std::vector<uint8_t> tagMemory;
uint16_t readTagSize() { return tagMemory.size(); }
bool robustPageRead(uint8_t page, uint8_t* output) {
    size_t offset = (page - 4) * 4;
    if (offset + 4 > tagMemory.size()) return false;
    std::memcpy(output, tagMemory.data() + offset, 4);
    return true;
}
void yield() {}
void esp_task_wdt_reset() {}
void vTaskDelay(int) {}
int pdMS_TO_TICKS(int milliseconds) { return milliseconds; }
int nfcReaderState = 0, movedSpool = 0, moveCount = 0, message = 0;
String movedUid;
int tr(int text) { return text; }
void oledShowProgressBar(int, int, int, int text) { message = text; }
void oledShowProgressBar(int, int, int, const char*) {}
void oledSetPriority(int, int) {}
void sendTagDataAsync(String) {}
void sendLocationAsync(int spool, String uid, int location, String) {
    if (location != 7) std::abort();
    movedSpool = spool;
    movedUid = uid;
    ++moveCount;
}

// PRODUCTION_HANDLERS

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::string scenario = argv[1];
    doc["location_id"] = 7;
    if (scenario.find("fast-") == 0) {
        bool extended = scenario.find("extended") != std::string::npos;
        failDecode = scenario.find("error") != std::string::npos;
        const std::string payload = "{\"sm_id\":\"42\"}";
        const std::string mime = extended ? "application/json" : "";
        tagMemory = {0x03, static_cast<uint8_t>(3 + mime.size() + payload.size()),
                     0xD2, static_cast<uint8_t>(mime.size()), static_cast<uint8_t>(payload.size())};
        tagMemory.insert(tagMemory.end(), mime.begin(), mime.end());
        tagMemory.insert(tagMemory.end(), payload.begin(), payload.end());
        // End on a page boundary so the production complete-read loop sees it.
        tagMemory.resize((tagMemory.size() + 3) / 4 * 4, 0);
        tagMemory.insert(tagMemory.end(), {0xFE, 0, 0, 0});
        doc.values.clear();
        doc["sm_id"] = "42";
        if (!quickSpoolIdCheck("01:02:03:04")) {
            std::cerr << "Expected accepted fast-path spool\n";
            return 1;
        }
        doc["location_id"] = 7;
        scanLocation();
        if (moveCount != 1 || movedSpool != 42 || movedUid != "01:02:03:04") {
            std::cerr << "Fast-path full read lost accepted spool: ID=" << movedSpool
                      << ", UID='" << movedUid.value << "'\n";
            return 1;
        }
        scanLocation();
        return moveCount != 1 || message != STR_SCAN_SPOOL_FIRST;
    }
    if (scenario == "replace" || scenario == "id") lastSpoolId = "42";
    if (scenario == "scan-request" || scenario == "unknown-scan-request") scanRequestActive = true;
    if (scenario == "uid" || scenario == "replace" || scenario == "scan-request") {
        uint8_t uid[] = {1, 2, 3, 4};
        detectBambuTag(uid, 4);
    }
    if (scenario == "extended" || scenario == "decoded" || scenario == "legacy" || scenario == "unknown"
        || scenario == "numeric" || scenario == "numeric-legacy" || scenario == "unknown-scan-request") {
        doc.values.clear();
        if (scenario == "extended") {
            lastSpoolId = "42";
            doc["protocol"] = "openspool";
        } else if (scenario == "unknown") lastSpoolId = "42";
        else if (scenario == "numeric" || scenario == "numeric-legacy") {
            lastSpoolId = "42";
            doc[scenario == "numeric" ? "spool_id" : "sm_id"] = 99;
        } else if (scenario == "unknown-scan-request") doc["vendor"] = "Example";
        else doc[scenario == "legacy" ? "sm_id" : "spool_id"] = "99";
        scanDecodedTag("01:02:03:04");
        doc["location_id"] = 7;
    }
    scanLocation();
    if (scenario == "uid" || scenario == "replace" || scenario == "extended" || scenario == "unknown"
        || scenario == "numeric" || scenario == "numeric-legacy") {
        if (moveCount != 1 || movedSpool != 0 || movedUid != "01:02:03:04") {
            std::cerr << "Expected UID-only location request for new spool; got ID "
                      << movedSpool << " and UID '" << movedUid.value << "'\n";
            return 1;
        }
    } else if (scenario == "empty" || scenario == "scan-request" || scenario == "unknown-scan-request") {
        if (moveCount != 0 || message != STR_SCAN_SPOOL_FIRST) return 1;
    } else if (scenario == "id") {
        if (moveCount != 1 || movedSpool != 42 || movedUid.length() != 0) return 1;
    } else if (scenario == "decoded" || scenario == "legacy") {
        if (moveCount != 1 || movedSpool != 99 || movedUid != "01:02:03:04") return 1;
    } else return 2;
    int previousMoves = moveCount;
    scanLocation();
    if (moveCount != previousMoves || message != STR_SCAN_SPOOL_FIRST) {
        std::cerr << "Location context must be consumed once\n";
        return 1;
    }
}
