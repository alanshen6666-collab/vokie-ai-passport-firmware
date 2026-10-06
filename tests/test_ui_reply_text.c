#include "ui_reply_text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_page_count_short_text(void)
{
    assert(ui_reply_page_count("") == 1);
    assert(ui_reply_page_count("hello") == 1);
    assert(ui_reply_page_count("OpenClaw 回复") == 1);
}

static void test_narrow_and_wide_units(void)
{
    // A full-width glyph occupies two display units.
    assert(ui_reply_codepoint_units('A') == 1);
    assert(ui_reply_codepoint_units(0x4E2D) == 2); // 中
    assert(ui_reply_codepoint_units(0x3002) == 2); // 。
    assert(ui_reply_codepoint_units('\n') == 1);

    // One more glyph than a page holds must spill to a second page.
    char narrow[UI_REPLY_UNITS_PER_PAGE + 2];
    for (size_t index = 0; index < sizeof(narrow) - 1; ++index) narrow[index] = 'a';
    narrow[sizeof(narrow) - 1] = '\0';
    assert(ui_reply_page_count(narrow) == 2);

    // Half as many full-width glyphs need the same space. Each glyph is three
    // UTF-8 bytes, so size the buffer by bytes, not by characters.
    char wide[(UI_REPLY_UNITS_PER_PAGE / 2) * 3 + 4];
    size_t offset = 0;
    for (int index = 0; index < UI_REPLY_UNITS_PER_PAGE / 2; ++index)
    {
        wide[offset++] = (char)0xE4;
        wide[offset++] = (char)0xB8;
        wide[offset++] = (char)0xAD; // 中
    }
    wide[offset] = '\0';
    assert(ui_reply_page_count(wide) == 1);

    wide[offset++] = (char)0xE4;
    wide[offset++] = (char)0xB8;
    wide[offset++] = (char)0xAD;
    wide[offset] = '\0';
    assert(ui_reply_page_count(wide) == 2);
}

static void test_newline_forces_page_break(void)
{
    assert(ui_reply_page_count("a\nb") == 2);

    char page[16];
    size_t length = ui_reply_page_copy("a\nb", 0, page, sizeof(page));
    assert(length == 2);
    assert(strcmp(page, "a\n") == 0);
    length = ui_reply_page_copy("a\nb", 1, page, sizeof(page));
    assert(length == 1);
    assert(strcmp(page, "b") == 0);
}

static void test_pages_reassemble_original(void)
{
    const char *text =
        "第一页的内容比较长，需要换行和分页。\n"
        "Second line with ASCII text that wraps around.\n"
        "第三行：混合内容 mixed content 12345。";
    char joined[2048] = {0};
    const int pages = ui_reply_page_count(text);
    assert(pages >= 2);
    char page[512];
    for (int index = 0; index < pages; ++index)
    {
        ui_reply_page_copy(text, index, page, sizeof(page));
        assert(strlen(joined) + strlen(page) < sizeof(joined));
        strcat(joined, page);
    }
    assert(strcmp(joined, text) == 0);
}

static void test_invalid_utf8_does_not_stall(void)
{
    const char text[] = "ok\xFF\xFEtail";
    assert(ui_reply_page_count(text) >= 1);
    char page[16];
    ui_reply_page_copy(text, 0, page, sizeof(page));
    assert(strlen(page) > 0);
}

int main(void)
{
    test_page_count_short_text();
    test_narrow_and_wide_units();
    test_newline_forces_page_break();
    test_pages_reassemble_original();
    test_invalid_utf8_does_not_stall();
    printf("test_ui_reply_text: PASS\n");
    return 0;
}
