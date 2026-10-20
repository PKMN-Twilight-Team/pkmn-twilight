#include "global.h"
#include "event_data.h"
#include "league_cards.h"
#include "string_util.h"
#include "text.h"
#include "test/overworld_script.h"
#include "test/test.h"

// Keep in sync with the limits listed in src/data/league_cards.h and localization/rules.json.
#define NAME_FONT        FONT_NORMAL
#define NAME_WIDTH       54
#define TITLE_FONT       FONT_SMALL
#define TITLE_WIDTH      54
#define TITLE_LINES      2
#define LABEL_FONT       FONT_SMALL
#define LABEL_WIDTH      56
#define FACT_FONT        FONT_NORMAL
#define FACT_WIDTH       208
#define FACT_PAGE_LINES  2

static void ClearLeagueCards(void)
{
    memset(gSaveBlock1Ptr->leagueCards, 0, sizeof(gSaveBlock1Ptr->leagueCards));
}

// Checks every line of 'str' against 'maxWidth', and every page ('\p') against
// 'maxLines'. Returns the widest line.
static u32 MeasureText(const u8 *str, u32 fontId, u32 *maxLinesOnPage)
{
    u8 line[256];
    u32 i = 0, widest = 0, lines = 1;

    *maxLinesOnPage = 1;
    while (TRUE)
    {
        u8 c = *str++;
        if (c == EOS || c == CHAR_NEWLINE || c == CHAR_PROMPT_SCROLL || c == CHAR_PROMPT_CLEAR)
        {
            u32 width;
            line[i] = EOS;
            width = GetStringWidth(fontId, line, 0);
            if (width > widest)
                widest = width;
            i = 0;
            if (c == EOS)
                break;
            if (c == CHAR_PROMPT_CLEAR)
                lines = 1;
            else
                lines++;
            if (lines > *maxLinesOnPage)
                *maxLinesOnPage = lines;
        }
        else if (i < ARRAY_COUNT(line) - 1)
        {
            line[i++] = c;
        }
    }
    return widest;
}

TEST("League Card names and titles fit on the card")
{
    u32 card, lines;
    const struct LeagueCard *info = NULL;

    for (card = LEAGUE_CARD_NONE + 1; card < LEAGUE_CARD_COUNT; card++)
    {
        PARAMETRIZE_LABEL("%S", LeagueCard_GetInfo(card)->name) { info = LeagueCard_GetInfo(card); }
    }
    EXPECT_LE(MeasureText(info->name, NAME_FONT, &lines), NAME_WIDTH);
    EXPECT_EQ(lines, 1);
    EXPECT_LE(MeasureText(info->title, TITLE_FONT, &lines), TITLE_WIDTH);
    EXPECT_LE(lines, TITLE_LINES);
    EXPECT_LE(info->numFacts, LEAGUE_CARD_MAX_FACTS);
}

TEST("League Card facts fit on the card and in the dialogue box")
{
    u32 card, fact, lines;
    const struct LeagueFact *info = NULL;

    for (card = LEAGUE_CARD_NONE + 1; card < LEAGUE_CARD_COUNT; card++)
    {
        for (fact = 0; fact < LeagueCard_GetInfo(card)->numFacts; fact++)
        {
            PARAMETRIZE_LABEL("%S", LeagueCard_GetInfo(card)->facts[fact].label) { info = &LeagueCard_GetInfo(card)->facts[fact]; }
        }
    }
    EXPECT_LE(MeasureText(info->label, LABEL_FONT, &lines), LABEL_WIDTH);
    EXPECT_EQ(lines, 1);
    EXPECT_LE(MeasureText(info->text, FACT_FONT, &lines), FACT_WIDTH);
    EXPECT_LE(lines, FACT_PAGE_LINES);
}

TEST("League Card facts learned before the card is unlocked stay hidden")
{
    ClearLeagueCards();

    EXPECT_EQ(LeagueCard_LearnFact(LEAGUE_CARD_COLLIE, 1), FALSE);
    EXPECT(LeagueCard_KnowsFact(LEAGUE_CARD_COLLIE, 1));
    EXPECT(!LeagueCard_IsFactVisible(LEAGUE_CARD_COLLIE, 1));
    EXPECT(!LeagueCard_IsUnlocked(LEAGUE_CARD_COLLIE));
    EXPECT(!LeagueCard_IsUnread(LEAGUE_CARD_COLLIE));
    EXPECT_EQ(LeagueCard_CountUnlocked(), 0);

    LeagueCard_Unlock(LEAGUE_CARD_COLLIE);
    EXPECT(LeagueCard_IsFactVisible(LEAGUE_CARD_COLLIE, 1));
    EXPECT(LeagueCard_IsFactUnread(LEAGUE_CARD_COLLIE, 1));
    EXPECT(LeagueCard_IsUnread(LEAGUE_CARD_COLLIE));
    EXPECT_EQ(LeagueCard_CountUnlocked(), 1);
}

TEST("League Cards are unread until viewed and every visible fact is read")
{
    ClearLeagueCards();

    LeagueCard_Unlock(LEAGUE_CARD_SPRUCE);
    EXPECT(LeagueCard_IsUnread(LEAGUE_CARD_SPRUCE));
    gSaveBlock1Ptr->leagueCards[LEAGUE_CARD_SPRUCE].viewed = TRUE;
    EXPECT(!LeagueCard_IsUnread(LEAGUE_CARD_SPRUCE));

    EXPECT_EQ(LeagueCard_LearnFact(LEAGUE_CARD_SPRUCE, 0), TRUE);
    EXPECT_EQ(LeagueCard_LearnFact(LEAGUE_CARD_SPRUCE, 0), FALSE);
    EXPECT(LeagueCard_IsUnread(LEAGUE_CARD_SPRUCE));
    LeagueCard_MarkFactRead(LEAGUE_CARD_SPRUCE, 0);
    EXPECT(!LeagueCard_IsUnread(LEAGUE_CARD_SPRUCE));

    // Forgetting and relearning a fact makes it unread again.
    LeagueCard_ForgetFact(LEAGUE_CARD_SPRUCE, 0);
    EXPECT(!LeagueCard_KnowsFact(LEAGUE_CARD_SPRUCE, 0));
    LeagueCard_LearnFact(LEAGUE_CARD_SPRUCE, 0);
    EXPECT(LeagueCard_IsFactUnread(LEAGUE_CARD_SPRUCE, 0));
}

TEST("League Card functions ignore invalid cards and facts")
{
    ClearLeagueCards();

    LeagueCard_Unlock(LEAGUE_CARD_NONE);
    LeagueCard_Unlock(LEAGUE_CARD_COUNT);
    EXPECT_EQ(LeagueCard_CountUnlocked(), 0);
    EXPECT_EQ(LeagueCard_LearnFact(LEAGUE_CARD_COLLIE, LEAGUE_CARD_MAX_FACTS), FALSE);
    EXPECT_EQ(LeagueCard_LearnFact(LEAGUE_CARD_COLLIE, LeagueCard_GetInfo(LEAGUE_CARD_COLLIE)->numFacts), FALSE);
    EXPECT_EQ(LeagueCard_CountKnownFacts(LEAGUE_CARD_COLLIE), 0);
}

TEST("League Card script commands")
{
    ClearLeagueCards();

    RUN_OVERWORLD_SCRIPT(
        learnleaguefact LEAGUE_CARD_COLLIE, 2;
        copyvar VAR_TEMP_0, VAR_RESULT;
        checkleaguecard LEAGUE_CARD_COLLIE;
        copyvar VAR_TEMP_1, VAR_RESULT;
        checkleaguefact LEAGUE_CARD_COLLIE, 2;
        copyvar VAR_TEMP_2, VAR_RESULT;
        unlockleaguecard LEAGUE_CARD_COLLIE;
        learnleaguefact LEAGUE_CARD_COLLIE, 3;
        copyvar VAR_TEMP_3, VAR_RESULT;
        checkleaguecard LEAGUE_CARD_COLLIE;
        copyvar VAR_TEMP_4, VAR_RESULT;
        forgetleaguefact LEAGUE_CARD_COLLIE, 2;
        checkleaguefact LEAGUE_CARD_COLLIE, 2;
        copyvar VAR_TEMP_5, VAR_RESULT;
        lockleaguecard LEAGUE_CARD_COLLIE;
        checkleaguecard LEAGUE_CARD_COLLIE;
        copyvar VAR_TEMP_6, VAR_RESULT;
    );

    EXPECT_EQ(VarGet(VAR_TEMP_0), FALSE); // learned while locked: not shown yet
    EXPECT_EQ(VarGet(VAR_TEMP_1), FALSE);
    EXPECT_EQ(VarGet(VAR_TEMP_2), TRUE);
    EXPECT_EQ(VarGet(VAR_TEMP_3), TRUE);  // learned while unlocked: shown
    EXPECT_EQ(VarGet(VAR_TEMP_4), TRUE);
    EXPECT_EQ(VarGet(VAR_TEMP_5), FALSE);
    EXPECT_EQ(VarGet(VAR_TEMP_6), FALSE);
    EXPECT(LeagueCard_KnowsFact(LEAGUE_CARD_COLLIE, 3));
}
