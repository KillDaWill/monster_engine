#include "interface/UiText.h"

uint32_t UiText_Next(const char *text, size_t *offset)
{
    const unsigned char *s = (const unsigned char *)text + *offset;
    if (!s[0]) return 0;
    ++*offset;
    if (s[0] < 0x80) return s[0];
    if (s[0] < 0xc2 || s[0] > 0xf4) return 0xfffd;
    unsigned length = s[0] < 0xe0 ? 2 : s[0] < 0xf0 ? 3 : 4;
    uint32_t value = s[0] & (0x7f >> length);
    for (unsigned i = 1; i < length; ++i) {
        if ((s[i] & 0xc0) != 0x80) return 0xfffd;
        value = (value << 6) | (s[i] & 0x3f);
    }
    if ((length == 3 && value < 0x800) || (length == 4 && value < 0x10000) ||
        (value >= 0xd800 && value <= 0xdfff) || value > 0x10ffff) return 0xfffd;
    *offset += length - 1;
    return value;
}

size_t UiText_Previous(const char *text, size_t offset)
{
    if (!offset) return 0;
    size_t start = offset - 1;
    while (start && offset - start < 4 && ((unsigned char)text[start] & 0xc0) == 0x80)
        --start;
    size_t end = start;
    UiText_Next(text, &end);
    return end == offset ? start : offset - 1;
}

size_t UiText_Count(const char *text, size_t bytes)
{
    size_t offset = 0, count = 0;
    while (offset < bytes && UiText_Next(text, &offset)) ++count;
    return count;
}

size_t UiText_Wrap(const char *text, int columns, size_t *advance)
{
    size_t offset = 0, wordBreak = 0;
    int seenWord = 0;
    for (int i = 0; text[offset] && i < (columns > 0 ? columns : 1); ++i) {
        if (UiText_Next(text, &offset) == ' ') {
            if (seenWord) wordBreak = offset;
        } else {
            seenWord = 1;
        }
    }
    if (text[offset] == ' ' && seenWord) {
        *advance = offset + 1;
        return offset;
    }
    if (text[offset] && wordBreak) offset = wordBreak;
    *advance = offset;
    return offset;
}
