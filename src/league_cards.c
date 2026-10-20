#include "global.h"
#include "league_cards.h"
#include "bg.h"
#include "data.h"
#include "gpu_regs.h"
#include "item_menu.h"
#include "list_menu.h"
#include "localization.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "menu_helpers.h"
#include "overworld.h"
#include "palette.h"
#include "scanline_effect.h"
#include "script.h"
#include "sound.h"
#include "string_util.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "trainer_pokemon_sprites.h"
#include "window.h"
#include "constants/rgb.h"
#include "constants/songs.h"
#include "constants/trainers.h"

// League Cards: a trainer card for each notable trainer, kept in the Card Case.
// The list on the left only shows unlocked cards. The card on the right shows
// the trainer's portrait on the front and up to LEAGUE_CARD_MAX_FACTS facts on
// the back, and each fact prints its text in the dialogue box.
// See docs/tutorials/how_to_league_cards.md.

// Set to TRUE once the art in graphics/league_cards/ exists, and create
// src/data/league_cards_graphics.h as described in the tutorial. Until then the
// screen is drawn from solid colours.
#define LEAGUE_CARDS_CUSTOM_GFX FALSE

STATIC_ASSERT(LEAGUE_CARD_COUNT <= LEAGUE_CARD_SAVE_SLOTS, LeagueCardCountExceedsSaveSlots);
STATIC_ASSERT(LEAGUE_CARD_COUNT <= 0xFF, LeagueCardCountTooLarge);

#include "data/league_cards.h"

#if LEAGUE_CARDS_CUSTOM_GFX
#include "data/league_cards_graphics.h"
#endif

//------------------------------------------------------------------------------
// Save data
//------------------------------------------------------------------------------

static struct LeagueCardSave *GetCardSave(u32 card)
{
    if (card == LEAGUE_CARD_NONE || card >= LEAGUE_CARD_COUNT)
        return NULL;
    return &gSaveBlock1Ptr->leagueCards[card];
}

static bool32 IsValidFact(u32 card, u32 fact)
{
    return card != LEAGUE_CARD_NONE && card < LEAGUE_CARD_COUNT && fact < sLeagueCards[card].numFacts;
}

const struct LeagueCard *LeagueCard_GetInfo(u32 card)
{
    if (card >= LEAGUE_CARD_COUNT)
        card = LEAGUE_CARD_NONE;
    return &sLeagueCards[card];
}

bool32 LeagueCard_IsUnlocked(u32 card)
{
    struct LeagueCardSave *save = GetCardSave(card);
    return save != NULL && save->unlocked;
}

// Facts learned while the card was locked become visible now, and count as unread.
void LeagueCard_Unlock(u32 card)
{
    struct LeagueCardSave *save = GetCardSave(card);
    if (save == NULL || save->unlocked)
        return;
    save->unlocked = TRUE;
    save->viewed = FALSE;
}

void LeagueCard_Lock(u32 card)
{
    struct LeagueCardSave *save = GetCardSave(card);
    if (save == NULL)
        return;
    save->unlocked = FALSE;
    save->viewed = FALSE;
}

bool32 LeagueCard_KnowsFact(u32 card, u32 fact)
{
    if (!IsValidFact(card, fact))
        return FALSE;
    return (gSaveBlock1Ptr->leagueCards[card].knownFacts >> fact) & 1;
}

bool32 LeagueCard_IsFactVisible(u32 card, u32 fact)
{
    return LeagueCard_IsUnlocked(card) && LeagueCard_KnowsFact(card, fact);
}

// A fact can be learned before its card is unlocked. It is stored silently and
// shows up once the card is unlocked. Returns TRUE if the fact is new and can
// be seen in the Card Case right now.
bool32 LeagueCard_LearnFact(u32 card, u32 fact)
{
    struct LeagueCardSave *save;

    if (!IsValidFact(card, fact) || LeagueCard_KnowsFact(card, fact))
        return FALSE;
    save = &gSaveBlock1Ptr->leagueCards[card];
    save->knownFacts |= 1 << fact;
    save->readFacts &= ~(1 << fact);
    return save->unlocked;
}

void LeagueCard_ForgetFact(u32 card, u32 fact)
{
    struct LeagueCardSave *save;

    if (!IsValidFact(card, fact))
        return;
    save = &gSaveBlock1Ptr->leagueCards[card];
    save->knownFacts &= ~(1 << fact);
    save->readFacts &= ~(1 << fact);
}

bool32 LeagueCard_IsFactUnread(u32 card, u32 fact)
{
    if (!LeagueCard_IsFactVisible(card, fact))
        return FALSE;
    return !((gSaveBlock1Ptr->leagueCards[card].readFacts >> fact) & 1);
}

void LeagueCard_MarkFactRead(u32 card, u32 fact)
{
    if (LeagueCard_IsFactVisible(card, fact))
        gSaveBlock1Ptr->leagueCards[card].readFacts |= 1 << fact;
}

bool32 LeagueCard_IsUnread(u32 card)
{
    u32 fact;
    struct LeagueCardSave *save = GetCardSave(card);

    if (save == NULL || !save->unlocked)
        return FALSE;
    if (!save->viewed)
        return TRUE;
    for (fact = 0; fact < sLeagueCards[card].numFacts; fact++)
    {
        if (LeagueCard_IsFactUnread(card, fact))
            return TRUE;
    }
    return FALSE;
}

u32 LeagueCard_CountUnlocked(void)
{
    u32 card, count = 0;

    for (card = LEAGUE_CARD_NONE + 1; card < LEAGUE_CARD_COUNT; card++)
    {
        if (LeagueCard_IsUnlocked(card))
            count++;
    }
    return count;
}

u32 LeagueCard_CountKnownFacts(u32 card)
{
    u32 fact, count = 0;

    for (fact = 0; fact < LeagueCard_GetInfo(card)->numFacts; fact++)
    {
        if (LeagueCard_KnowsFact(card, fact))
            count++;
    }
    return count;
}

//------------------------------------------------------------------------------
// Card Case screen
//------------------------------------------------------------------------------

enum
{
    WIN_HEADER,
    WIN_LIST,
    WIN_MESSAGE,
    WIN_PORTRAIT,
    WIN_CARD,
    WIN_COUNT,
};

enum
{
    STATE_FADE_IN,
    STATE_LIST,
    STATE_BACK,
    STATE_FLIP,
    STATE_PRINT_FACT,
    STATE_WAIT_FACT,
    STATE_EMPTY,
    STATE_FADE_OUT,
};

enum
{
    HELP_LIST,
    HELP_LIST_PAGED,
    HELP_BACK,
    HELP_BACK_MULTI,
    HELP_MESSAGE,
    HELP_EMPTY,
};

// Backgrounds
#define BG_TEXT     0 // header, list and dialogue box
#define BG_PORTRAIT 1 // trainer portrait (flips)
#define BG_CARD     2 // card body (flips)
#define BG_BACKDROP 3 // custom background art, if any

// BG palettes
#define PAL_BACKDROP     0
#define PAL_PORTRAIT     12
#define PAL_CARD         13
#define PAL_MESSAGE_BOX  14
#define PAL_TEXT         15
#define PAL_FRAME        11

// BG_TEXT tiles
#define TILE_MESSAGE_BOX 0x1A0
#define TILE_FRAME       0x1D0

// Layout, in tiles
#define LIST_LEFT        1
#define LIST_TOP         3
#define LIST_WIDTH       9
#define LIST_ROWS        5
#define CARD_LEFT        12
#define CARD_TOP         2
#define CARD_WIDTH       17
#define CARD_HEIGHT      12

// Layout, in pixels, inside WIN_CARD
#define CARD_PX_WIDTH    (CARD_WIDTH * 8)
#define CARD_PX_HEIGHT   (CARD_HEIGHT * 8)
#define PORTRAIT_X       64 // the portrait window sits on the tile grid
#define PORTRAIT_Y       16
#define CELL_COLUMNS     2
#define CELL_ROWS        (LEAGUE_CARD_MAX_FACTS / CELL_COLUMNS)
#define CELL_MARGIN      4
#define CELL_WIDTH       ((CARD_PX_WIDTH - CELL_MARGIN * (CELL_COLUMNS + 1)) / CELL_COLUMNS)
#define CELL_HEIGHT      ((CARD_PX_HEIGHT - CELL_MARGIN * (CELL_ROWS + 1)) / CELL_ROWS)

// Flip animation: the card is squashed toward its middle row, redrawn, then
// stretched back out.
#define CARD_MID_Y       ((CARD_TOP * 8) + CARD_PX_HEIGHT / 2)
#define FLIP_HALF_HEIGHT (CARD_PX_HEIGHT / 2)
#define FLIP_SHRINK_STEP 8
#define FLIP_GROW_STEP   6

#define TAG_SCROLL_ARROWS 0x1500

// Card palette indices (PAL_CARD). A custom card palette must keep 1-3 for
// text and 4-12 for the drawn parts below.
enum
{
    CARD_COLOR_TRANSPARENT,
    CARD_COLOR_WHITE,
    CARD_COLOR_TEXT,
    CARD_COLOR_SHADOW,
    CARD_COLOR_FRONT,
    CARD_COLOR_FRONT_BORDER,
    CARD_COLOR_BACK,
    CARD_COLOR_BACK_BORDER,
    CARD_COLOR_CELL,
    CARD_COLOR_CELL_NEW,
    CARD_COLOR_CELL_LOCKED,
    CARD_COLOR_CURSOR,
    CARD_COLOR_ACCENT,
};

struct LeagueCardsMenu
{
    MainCallback exitCallback;
    struct ListMenuItem listItems[LEAGUE_CARD_COUNT];
    u8 cardCount;
    u8 listTaskId;
    u8 scrollArrowsTaskId;
    u8 state;
    u8 stateAfterFlip;
    u8 card;             // card being shown
    u8 flipToCard;
    bool8 onBack;
    bool8 flipToBack;
    bool8 flipping;
    bool8 redrawList;
    bool8 cardDrawn;
    u8 factCursor;
    s16 flipHalfHeight;  // visible half-height of the card during a flip
    bool8 flipGrowing;
    u16 scrollOffset;
    u16 selectedRow;
};

static EWRAM_DATA struct LeagueCardsMenu *sMenu = NULL;

static void CB2_SetupLeagueCards(void);
static void CB2_LeagueCards(void);
static void VBlankCB_LeagueCards(void);
static void HBlankCB_CardFlip(void);
static bool8 SetupLeagueCards(void);
static void InitBackgroundsAndWindows(void);
static void LoadPalettes(void);
static void BuildList(void);
static void CreateList(void);
static void UpdateScrollArrows(void);
static void ListCursorMoved(s32 itemIndex, bool8 onInit, struct ListMenu *list);
static void ListPrintUnreadMarker(u8 windowId, u32 card, u8 y);
static void PrintHeader(void);
static void PrintHelp(u32 help);
static void PrintHint(const u8 *text);
static void DrawCard(void);
static void DrawCardFront(void);
static void DrawCardBack(void);
static void DrawFactCell(u32 fact, bool32 copy);
static void MarkCardViewed(void);
static void StartFlip(u32 card, bool32 toBack, u32 nextState);
static void UpdateFlip(void);
static void SetFlipScanlines(s32 halfHeight);
static void Task_LeagueCards(u8 taskId);
static void HandleListInput(u8 taskId);
static void HandleBackInput(void);
static void EnterBackState(void);
static void EnterListState(void);
static void StartClose(void);
static void FreeLeagueCards(void);

static const u8 sText_Title[] = _("LEAGUE CARDS");
static const u8 sText_HelpList[] = _("{DPAD_UPDOWN}Pick {A_BUTTON}Flip {B_BUTTON}Exit");
static const u8 sText_HelpListPaged[] = _("{L_BUTTON}{R_BUTTON}Page {A_BUTTON}Flip {B_BUTTON}Exit");
static const u8 sText_HelpBack[] = _("{DPAD_NONE}Pick {A_BUTTON}Read {B_BUTTON}Flip");
static const u8 sText_HelpBackMulti[] = _("{L_BUTTON}{R_BUTTON}Card {A_BUTTON}Read {B_BUTTON}Flip");
static const u8 sText_HelpMessage[] = _("{A_BUTTON}OK");
static const u8 sText_HelpEmpty[] = _("{B_BUTTON}Exit");
static const u8 sText_HintList[] = _("Which trainer's card will you\nlook at?");
static const u8 sText_HintBack[] = _("Which fact will you read?");
static const u8 sText_HintEmpty[] = _("There are no cards in the\nCard Case yet.");
static const u8 sText_UnknownFact[] = _("?????");
static const u8 sText_FactCount[] = _("Facts {DYNAMIC 0}/{DYNAMIC 1}");
static const u8 sText_UnreadMarker[] = _("!");

static const u8 *const sHelpTexts[] =
{
    [HELP_LIST]       = sText_HelpList,
    [HELP_LIST_PAGED] = sText_HelpListPaged,
    [HELP_BACK]       = sText_HelpBack,
    [HELP_BACK_MULTI] = sText_HelpBackMulti,
    [HELP_MESSAGE]    = sText_HelpMessage,
    [HELP_EMPTY]      = sText_HelpEmpty,
};

static const u16 sBackdropPalette[] = {RGB(5, 9, 15)};

static const u16 sCardPalette[16] =
{
    [CARD_COLOR_TRANSPARENT]  = RGB(0, 0, 0),
    [CARD_COLOR_WHITE]        = RGB(31, 31, 31),
    [CARD_COLOR_TEXT]         = RGB(7, 7, 7),
    [CARD_COLOR_SHADOW]       = RGB(22, 22, 20),
    [CARD_COLOR_FRONT]        = RGB(29, 27, 21),
    [CARD_COLOR_FRONT_BORDER] = RGB(23, 14, 4),
    [CARD_COLOR_BACK]         = RGB(9, 15, 24),
    [CARD_COLOR_BACK_BORDER]  = RGB(4, 7, 14),
    [CARD_COLOR_CELL]         = RGB(28, 29, 31),
    [CARD_COLOR_CELL_NEW]     = RGB(31, 28, 15),
    [CARD_COLOR_CELL_LOCKED]  = RGB(17, 19, 22),
    [CARD_COLOR_CURSOR]       = RGB(31, 16, 6),
    [CARD_COLOR_ACCENT]       = RGB(27, 7, 6),
};

static const u8 sTextColors_Header[] = {TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY};
static const u8 sTextColors_Help[] = {TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_DARK_GRAY};
static const u8 sTextColors_Unread[] = {TEXT_COLOR_WHITE, TEXT_COLOR_RED, TEXT_COLOR_LIGHT_RED};
static const u8 sTextColors_Card[] = {CARD_COLOR_TRANSPARENT, CARD_COLOR_TEXT, CARD_COLOR_SHADOW};
static const u8 sTextColors_CardBack[] = {CARD_COLOR_TRANSPARENT, CARD_COLOR_WHITE, CARD_COLOR_BACK_BORDER};
static const u8 sTextColors_CardAccent[] = {CARD_COLOR_TRANSPARENT, CARD_COLOR_ACCENT, CARD_COLOR_SHADOW};

static const struct BgTemplate sBgTemplates[] =
{
    {
        .bg = BG_TEXT,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .priority = 0,
    },
    {
        .bg = BG_PORTRAIT,
        .charBaseIndex = 1,
        .mapBaseIndex = 30,
        .priority = 1,
    },
    {
        .bg = BG_CARD,
        .charBaseIndex = 2,
        .mapBaseIndex = 29,
        .priority = 2,
    },
    {
        .bg = BG_BACKDROP,
        .charBaseIndex = 3,
        .mapBaseIndex = 28,
        .priority = 3,
    },
};

static const struct WindowTemplate sWindowTemplates[] =
{
    [WIN_HEADER] =
    {
        .bg = BG_TEXT,
        .tilemapLeft = 0,
        .tilemapTop = 0,
        .width = 30,
        .height = 2,
        .paletteNum = PAL_TEXT,
        .baseBlock = 0x001,
    },
    [WIN_LIST] =
    {
        .bg = BG_TEXT,
        .tilemapLeft = LIST_LEFT,
        .tilemapTop = LIST_TOP,
        .width = LIST_WIDTH,
        .height = LIST_ROWS * 2,
        .paletteNum = PAL_TEXT,
        .baseBlock = 0x03D,
    },
    [WIN_MESSAGE] =
    {
        .bg = BG_TEXT,
        .tilemapLeft = 2,
        .tilemapTop = 15,
        .width = 27,
        .height = 4,
        .paletteNum = PAL_TEXT,
        .baseBlock = 0x097,
    },
    [WIN_PORTRAIT] =
    {
        .bg = BG_PORTRAIT,
        .tilemapLeft = CARD_LEFT + PORTRAIT_X / 8,
        .tilemapTop = CARD_TOP + PORTRAIT_Y / 8,
        .width = TRAINER_PIC_WIDTH / 8,
        .height = TRAINER_PIC_HEIGHT / 8,
        .paletteNum = PAL_PORTRAIT,
        .baseBlock = 0x001,
    },
    [WIN_CARD] =
    {
        .bg = BG_CARD,
        .tilemapLeft = CARD_LEFT,
        .tilemapTop = CARD_TOP,
        .width = CARD_WIDTH,
        .height = CARD_HEIGHT,
        .paletteNum = PAL_CARD,
        .baseBlock = 0x001,
    },
    DUMMY_WIN_TEMPLATE,
};

STATIC_ASSERT(PORTRAIT_X % 8 == 0 && PORTRAIT_Y % 8 == 0, PortraitNotTileAligned);

//------------------------------------------------------------------------------
// Opening and closing
//------------------------------------------------------------------------------

void LeagueCards_Open(MainCallback exitCallback)
{
    sMenu = AllocZeroed(sizeof(*sMenu));
    if (sMenu == NULL)
    {
        SetMainCallback2(exitCallback);
        return;
    }
    sMenu->exitCallback = exitCallback;
    sMenu->listTaskId = TASK_NONE;
    sMenu->scrollArrowsTaskId = TASK_NONE;
    gMain.state = 0;
    SetMainCallback2(CB2_SetupLeagueCards);
}

void CB2_OpenLeagueCardsFromBag(void)
{
    LeagueCards_Open(CB2_ReturnToBagMenuPocket);
}

static void CB2_SetupLeagueCards(void)
{
    while (!SetupLeagueCards())
    {
        if (MenuHelpers_ShouldWaitForLinkRecv())
            break;
    }
}

static bool8 SetupLeagueCards(void)
{
    switch (gMain.state)
    {
    case 0:
        SetVBlankHBlankCallbacksToNull();
        ClearScheduledBgCopiesToVram();
        ScanlineEffect_Stop();
        break;
    case 1:
        FreeAllSpritePalettes();
        ResetPaletteFade();
        ResetSpriteData();
        ResetTasks();
        break;
    case 2:
        ResetVramOamAndBgCntRegs();
        InitBackgroundsAndWindows();
        break;
    case 3:
        LoadPalettes();
        break;
    case 4:
        BuildList();
        PrintHeader();
        if (sMenu->cardCount == 0)
        {
            PrintHelp(HELP_EMPTY);
            PrintHint(sText_HintEmpty);
        }
        else
        {
            PrintHelp(sMenu->cardCount > LIST_ROWS ? HELP_LIST_PAGED : HELP_LIST);
            PrintHint(sText_HintList);
        }
        break;
    case 5:
        // The list's first cursor callback draws the first card.
        if (sMenu->cardCount != 0)
            CreateList();
        else
            DrawCard();
        CopyWindowToVram(WIN_LIST, COPYWIN_FULL);
        break;
    case 6:
        BeginNormalPaletteFade(PALETTES_ALL, 0, 16, 0, RGB_BLACK);
        SetVBlankCallback(VBlankCB_LeagueCards);
        SetMainCallback2(CB2_LeagueCards);
        CreateTask(Task_LeagueCards, 0);
        PlaySE(SE_RG_CARD_OPEN);
        return TRUE;
    }
    gMain.state++;
    return FALSE;
}

static void InitBackgroundsAndWindows(void)
{
    ResetBgsAndClearDma3BusyFlags(0);
    InitBgsFromTemplates(0, sBgTemplates, ARRAY_COUNT(sBgTemplates));
    InitWindows(sWindowTemplates);
    DeactivateAllTextPrinters();

    FillBgTilemapBufferRect_Palette0(BG_TEXT, 0, 0, 0, 32, 32);
    FillBgTilemapBufferRect_Palette0(BG_PORTRAIT, 0, 0, 0, 32, 32);
    FillBgTilemapBufferRect_Palette0(BG_CARD, 0, 0, 0, 32, 32);

#if LEAGUE_CARDS_CUSTOM_GFX
    LoadBgTiles(BG_BACKDROP, gLeagueCardsBg_Gfx, sizeof(gLeagueCardsBg_Gfx), 0);
    LoadBgTilemap(BG_BACKDROP, gLeagueCardsBg_Tilemap, sizeof(gLeagueCardsBg_Tilemap), 0);
#endif

    LoadMessageBoxGfx(WIN_MESSAGE, TILE_MESSAGE_BOX, BG_PLTT_ID(PAL_MESSAGE_BOX));
    LoadUserWindowBorderGfx(WIN_LIST, TILE_FRAME, BG_PLTT_ID(PAL_FRAME));

    PutWindowTilemap(WIN_HEADER);
    PutWindowTilemap(WIN_LIST);
    PutWindowTilemap(WIN_MESSAGE);
    PutWindowTilemap(WIN_CARD);
    FillWindowPixelBuffer(WIN_HEADER, PIXEL_FILL(TEXT_COLOR_DARK_GRAY));
    FillWindowPixelBuffer(WIN_LIST, PIXEL_FILL(TEXT_COLOR_WHITE));
    FillWindowPixelBuffer(WIN_MESSAGE, PIXEL_FILL(TEXT_COLOR_WHITE));
    DrawStdFrameWithCustomTileAndPalette(WIN_LIST, FALSE, TILE_FRAME, PAL_FRAME);
    DrawDialogFrameWithCustomTileAndPalette(WIN_MESSAGE, FALSE, TILE_MESSAGE_BOX, PAL_MESSAGE_BOX);

    SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0 | DISPCNT_OBJ_ON | DISPCNT_OBJ_1D_MAP);
    SetGpuReg(REG_OFFSET_BLDCNT, BLDCNT_TGT1_BG1 | BLDCNT_TGT1_BG2 | BLDCNT_EFFECT_DARKEN);
    SetGpuReg(REG_OFFSET_BLDY, 0);
    ShowBg(BG_TEXT);
    ShowBg(BG_PORTRAIT);
    ShowBg(BG_CARD);
#if LEAGUE_CARDS_CUSTOM_GFX
    ShowBg(BG_BACKDROP);
#else
    HideBg(BG_BACKDROP);
#endif
    ScheduleBgCopyTilemapToVram(BG_TEXT);
    ScheduleBgCopyTilemapToVram(BG_PORTRAIT);
    ScheduleBgCopyTilemapToVram(BG_CARD);
}

static void LoadPalettes(void)
{
#if LEAGUE_CARDS_CUSTOM_GFX
    LoadPalette(gLeagueCardsBg_Pal, BG_PLTT_ID(PAL_BACKDROP), PLTT_SIZE_4BPP);
    LoadPalette(gLeagueCards_CardPal, BG_PLTT_ID(PAL_CARD), PLTT_SIZE_4BPP);
#else
    LoadPalette(sBackdropPalette, BG_PLTT_ID(PAL_BACKDROP), sizeof(sBackdropPalette));
    LoadPalette(sCardPalette, BG_PLTT_ID(PAL_CARD), sizeof(sCardPalette));
#endif
    LoadPalette(gStandardMenuPalette, BG_PLTT_ID(PAL_TEXT), PLTT_SIZE_4BPP);
}

static void CB2_LeagueCards(void)
{
    RunTasks();
    RunTextPrinters();
    AnimateSprites();
    BuildOamBuffer();
    DoScheduledBgTilemapCopiesToVram();
    UpdatePaletteFade();
}

static void VBlankCB_LeagueCards(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
    if (sMenu != NULL && sMenu->flipping)
    {
        CpuCopy16(gScanlineEffectRegBuffers[0], gScanlineEffectRegBuffers[1], DISPLAY_HEIGHT * sizeof(u16));
        SetGpuReg(REG_OFFSET_BG1VOFS, gScanlineEffectRegBuffers[1][0]);
        SetGpuReg(REG_OFFSET_BG2VOFS, gScanlineEffectRegBuffers[1][0]);
    }
}

// Runs at the end of each scanline and sets the offset for the next one.
static void HBlankCB_CardFlip(void)
{
    u16 line = REG_VCOUNT + 1;

    if (line < DISPLAY_HEIGHT)
    {
        REG_BG1VOFS = gScanlineEffectRegBuffers[1][line];
        REG_BG2VOFS = gScanlineEffectRegBuffers[1][line];
    }
}

static void StartClose(void)
{
    PlaySE(SE_PC_OFF);
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 16, RGB_BLACK);
    sMenu->state = STATE_FADE_OUT;
}

static void FreeLeagueCards(void)
{
    MainCallback exitCallback = sMenu->exitCallback;

    if (sMenu->scrollArrowsTaskId != TASK_NONE)
        RemoveScrollIndicatorArrowPair(sMenu->scrollArrowsTaskId);
    if (sMenu->listTaskId != TASK_NONE)
        DestroyListMenuTask(sMenu->listTaskId, NULL, NULL);
    SetHBlankCallback(NULL);
    DisableInterrupts(INTR_FLAG_HBLANK);
    SetGpuReg(REG_OFFSET_BLDCNT, 0);
    SetGpuReg(REG_OFFSET_BLDY, 0);
    FreeAllWindowBuffers();
    FREE_AND_SET_NULL(sMenu);
    SetMainCallback2(exitCallback);
}

//------------------------------------------------------------------------------
// List
//------------------------------------------------------------------------------

static void BuildList(void)
{
    u32 card;

    sMenu->cardCount = 0;
    for (card = LEAGUE_CARD_NONE + 1; card < LEAGUE_CARD_COUNT; card++)
    {
        if (!LeagueCard_IsUnlocked(card))
            continue;
        sMenu->listItems[sMenu->cardCount].name = sLeagueCards[card].name;
        sMenu->listItems[sMenu->cardCount].id = card;
        sMenu->cardCount++;
    }
    sMenu->card = (sMenu->cardCount != 0) ? sMenu->listItems[0].id : LEAGUE_CARD_NONE;
}

static const struct ListMenuTemplate sListTemplate =
{
    .moveCursorFunc = ListCursorMoved,
    .itemPrintFunc = ListPrintUnreadMarker,
    .windowId = WIN_LIST,
    .header_X = 0,
    .item_X = 8,
    .cursor_X = 0,
    .upText_Y = 1,
    .cursorPal = TEXT_COLOR_DARK_GRAY,
    .fillValue = TEXT_COLOR_WHITE,
    .cursorShadowPal = TEXT_COLOR_LIGHT_GRAY,
    .lettersSpacing = 0,
    .itemVerticalPadding = 0,
    .scrollMultiple = LIST_MULTIPLE_SCROLL_L_R,
    .fontId = FONT_NORMAL,
    .cursorKind = CURSOR_BLACK_ARROW,
};

static void CreateList(void)
{
    gMultiuseListMenuTemplate = sListTemplate;
    gMultiuseListMenuTemplate.items = sMenu->listItems;
    gMultiuseListMenuTemplate.totalItems = sMenu->cardCount;
    gMultiuseListMenuTemplate.maxShowed = min(sMenu->cardCount, LIST_ROWS);
    sMenu->listTaskId = ListMenuInit(&gMultiuseListMenuTemplate, sMenu->scrollOffset, sMenu->selectedRow);
    UpdateScrollArrows();
}

static void UpdateScrollArrows(void)
{
    if (sMenu->cardCount <= LIST_ROWS || sMenu->scrollArrowsTaskId != TASK_NONE)
        return;
    sMenu->scrollArrowsTaskId = AddScrollIndicatorArrowPairParameterized(
        SCROLL_ARROW_UP,
        (LIST_LEFT * 8) + (LIST_WIDTH * 4),
        (LIST_TOP * 8) - 4,
        (LIST_TOP + LIST_ROWS * 2) * 8 + 4,
        sMenu->cardCount - LIST_ROWS,
        TAG_SCROLL_ARROWS,
        TAG_SCROLL_ARROWS,
        &sMenu->scrollOffset);
}

// Selects list entry 'index' and scrolls it into view.
static void SetListIndex(u32 index)
{
    if (index < LIST_ROWS)
    {
        sMenu->scrollOffset = 0;
        sMenu->selectedRow = index;
    }
    else if (index >= sMenu->cardCount - LIST_ROWS)
    {
        sMenu->scrollOffset = sMenu->cardCount - LIST_ROWS;
        sMenu->selectedRow = index - sMenu->scrollOffset;
    }
    else
    {
        sMenu->scrollOffset = index - (LIST_ROWS - 1);
        sMenu->selectedRow = LIST_ROWS - 1;
    }
    DestroyListMenuTask(sMenu->listTaskId, NULL, NULL);
    sMenu->listTaskId = ListMenuInit(&gMultiuseListMenuTemplate, sMenu->scrollOffset, sMenu->selectedRow);
}

static u32 GetListIndex(void)
{
    return sMenu->scrollOffset + sMenu->selectedRow;
}

static void ListCursorMoved(s32 card, bool8 onInit, struct ListMenu *list)
{
    bool32 changed = (card != sMenu->card || !sMenu->cardDrawn);

    if (!onInit)
        PlaySE(SE_SELECT);
    sMenu->card = card;
    // While the back is showing, the list only follows along; the flip draws the card.
    // RedrawListMenu also lands here, so skip redrawing a card that is already shown.
    if (changed && (sMenu->state == STATE_FADE_IN || sMenu->state == STATE_LIST))
    {
        sMenu->onBack = FALSE;
        DrawCard();
        MarkCardViewed();
    }
}

static void ListPrintUnreadMarker(u8 windowId, u32 card, u8 y)
{
    s32 x = LIST_WIDTH * 8 - GetStringWidth(FONT_NORMAL, sText_UnreadMarker, 0) - 2;

    if (LeagueCard_IsUnread(card))
        AddTextPrinterParameterized4(windowId, FONT_NORMAL, x, y, 0, 0, sTextColors_Unread, TEXT_SKIP_DRAW, sText_UnreadMarker);
}

static void MarkCardViewed(void)
{
    struct LeagueCardSave *save = GetCardSave(sMenu->card);

    if (save != NULL && !save->viewed)
    {
        save->viewed = TRUE;
        sMenu->redrawList = TRUE;
    }
}

//------------------------------------------------------------------------------
// Header, help and dialogue box
//------------------------------------------------------------------------------

static void PrintHeader(void)
{
    AddTextPrinterParameterized3(WIN_HEADER, FONT_NORMAL, 4, 1, sTextColors_Header, TEXT_SKIP_DRAW, sText_Title);
    CopyWindowToVram(WIN_HEADER, COPYWIN_GFX);
}

// Button help, right-aligned in the header.
static void PrintHelp(u32 help)
{
    const u8 *text = sHelpTexts[help];
    s32 width = GetStringWidth(FONT_SMALL, text, 0);
    s32 left = 100;

    FillWindowPixelRect(WIN_HEADER, PIXEL_FILL(TEXT_COLOR_DARK_GRAY), left, 0, 240 - left, 16);
    AddTextPrinterParameterized3(WIN_HEADER, FONT_SMALL, 236 - width, 2, sTextColors_Help, TEXT_SKIP_DRAW, text);
    CopyWindowToVram(WIN_HEADER, COPYWIN_GFX);
}

static void PrintHint(const u8 *text)
{
    FillWindowPixelBuffer(WIN_MESSAGE, PIXEL_FILL(TEXT_COLOR_WHITE));
    AddTextPrinterParameterized2(WIN_MESSAGE, FONT_NORMAL, text, TEXT_SKIP_DRAW, NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
    CopyWindowToVram(WIN_MESSAGE, COPYWIN_FULL);
}

static void PrintFact(void)
{
    const struct LeagueFact *fact = &sLeagueCards[sMenu->card].facts[sMenu->factCursor];

    FillWindowPixelBuffer(WIN_MESSAGE, PIXEL_FILL(TEXT_COLOR_WHITE));
    gTextFlags.canABSpeedUpPrint = TRUE;
    AddTextPrinterParameterized2(WIN_MESSAGE, FONT_NORMAL, fact->text, GetPlayerTextSpeedDelay(), NULL, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_WHITE, TEXT_COLOR_LIGHT_GRAY);
    CopyWindowToVram(WIN_MESSAGE, COPYWIN_FULL);
}

//------------------------------------------------------------------------------
// Card drawing
//------------------------------------------------------------------------------

// FillWindowPixelRect works one pixel at a time, so large areas are filled with
// FillWindowPixelBuffer and borders are drawn as thin strips.
static void DrawFrame(u32 x, u32 y, u32 width, u32 height, u32 color, u32 thickness)
{
    FillWindowPixelRect(WIN_CARD, PIXEL_FILL(color), x, y, width, thickness);
    FillWindowPixelRect(WIN_CARD, PIXEL_FILL(color), x, y + height - thickness, width, thickness);
    FillWindowPixelRect(WIN_CARD, PIXEL_FILL(color), x, y + thickness, thickness, height - thickness * 2);
    FillWindowPixelRect(WIN_CARD, PIXEL_FILL(color), x + width - thickness, y + thickness, thickness, height - thickness * 2);
}

static void DrawBorderedRect(u32 x, u32 y, u32 width, u32 height, u32 fill, u32 border, u32 thickness)
{
    FillWindowPixelRect(WIN_CARD, PIXEL_FILL(fill), x + thickness, y + thickness, width - thickness * 2, height - thickness * 2);
    DrawFrame(x, y, width, height, border, thickness);
}

static void DrawCardBase(u32 fill, u32 border)
{
    FillWindowPixelBuffer(WIN_CARD, PIXEL_FILL(fill));
    DrawFrame(0, 0, CARD_PX_WIDTH, CARD_PX_HEIGHT, border, 3);
}

static void DrawCard(void)
{
    sMenu->cardDrawn = TRUE;
    if (sMenu->card == LEAGUE_CARD_NONE)
    {
        // No cards yet: an empty card outline.
        DrawCardBase(CARD_COLOR_CELL_LOCKED, CARD_COLOR_SHADOW);
        ClearWindowTilemap(WIN_PORTRAIT);
        ScheduleBgCopyTilemapToVram(BG_PORTRAIT);
        CopyWindowToVram(WIN_CARD, COPYWIN_FULL);
        return;
    }

    if (sMenu->onBack)
        DrawCardBack();
    else
        DrawCardFront();
}

static void DrawCardFront(void)
{
    const struct LeagueCard *info = &sLeagueCards[sMenu->card];
    u8 known[4], total[4];

#if LEAGUE_CARDS_CUSTOM_GFX
    BlitBitmapToWindow(WIN_CARD, (const u8 *)gLeagueCards_FrontGfx, 0, 0, CARD_PX_WIDTH, CARD_PX_HEIGHT);
#else
    DrawCardBase(CARD_COLOR_FRONT, CARD_COLOR_FRONT_BORDER);
    FillWindowPixelRect(WIN_CARD, PIXEL_FILL(CARD_COLOR_ACCENT), 3, 3, CARD_PX_WIDTH - 6, 2);
    DrawFrame(PORTRAIT_X - 2, PORTRAIT_Y - 2, TRAINER_PIC_WIDTH + 4, TRAINER_PIC_HEIGHT + 4, CARD_COLOR_FRONT_BORDER, 1);
#endif
    AddTextPrinterParameterized3(WIN_CARD, FONT_NORMAL, 6, 6, sTextColors_Card, TEXT_SKIP_DRAW, info->name);
    AddTextPrinterParameterized4(WIN_CARD, FONT_SMALL, 6, 24, 0, 0, sTextColors_CardAccent, TEXT_SKIP_DRAW, info->title);

    ConvertIntToDecimalStringN(known, LeagueCard_CountKnownFacts(sMenu->card), STR_CONV_MODE_LEFT_ALIGN, 1);
    ConvertIntToDecimalStringN(total, info->numFacts, STR_CONV_MODE_LEFT_ALIGN, 1);
    StringFormat2(gStringVar4, sText_FactCount, known, total);
    AddTextPrinterParameterized3(WIN_CARD, FONT_SMALL, 6, CARD_PX_HEIGHT - 18, sTextColors_Card, TEXT_SKIP_DRAW, gStringVar4);
    CopyWindowToVram(WIN_CARD, COPYWIN_GFX);

    // The portrait sits on its own background so the card colour shows through it.
    FillWindowPixelBuffer(WIN_PORTRAIT, PIXEL_FILL(0));
    CreateTrainerCardTrainerPicSprite(info->trainerPic, TRUE, 0, 0, PAL_PORTRAIT, WIN_PORTRAIT);
    PutWindowTilemap(WIN_PORTRAIT);
    CopyWindowToVram(WIN_PORTRAIT, COPYWIN_FULL);
}

static void DrawCardBack(void)
{
    u32 fact;

#if LEAGUE_CARDS_CUSTOM_GFX
    BlitBitmapToWindow(WIN_CARD, (const u8 *)gLeagueCards_BackGfx, 0, 0, CARD_PX_WIDTH, CARD_PX_HEIGHT);
#else
    DrawCardBase(CARD_COLOR_BACK, CARD_COLOR_BACK_BORDER);
#endif
    for (fact = 0; fact < LEAGUE_CARD_MAX_FACTS; fact++)
        DrawFactCell(fact, FALSE);
    CopyWindowToVram(WIN_CARD, COPYWIN_GFX);

    ClearWindowTilemap(WIN_PORTRAIT);
    ScheduleBgCopyTilemapToVram(BG_PORTRAIT);
}

static void DrawFactCell(u32 fact, bool32 copy)
{
    const struct LeagueCard *info = &sLeagueCards[sMenu->card];
    u32 x = CELL_MARGIN + (fact % CELL_COLUMNS) * (CELL_WIDTH + CELL_MARGIN);
    u32 y = CELL_MARGIN + (fact / CELL_COLUMNS) * (CELL_HEIGHT + CELL_MARGIN);
    u32 fill, border;
    const u8 *label;
    s32 labelX;

    if (fact >= info->numFacts)
    {
        // This card has fewer facts: leave the slot blank.
        return;
    }

    if (!LeagueCard_IsFactVisible(sMenu->card, fact))
    {
        fill = CARD_COLOR_CELL_LOCKED;
        label = sText_UnknownFact;
    }
    else
    {
        fill = LeagueCard_IsFactUnread(sMenu->card, fact) ? CARD_COLOR_CELL_NEW : CARD_COLOR_CELL;
        label = info->facts[fact].label;
    }
    border = (fact == sMenu->factCursor && sMenu->state != STATE_LIST) ? CARD_COLOR_CURSOR : CARD_COLOR_BACK_BORDER;

    DrawBorderedRect(x, y, CELL_WIDTH, CELL_HEIGHT, fill, border, 2);
    labelX = x + (CELL_WIDTH - GetStringWidth(FONT_SMALL, label, 0)) / 2;
    AddTextPrinterParameterized4(WIN_CARD, FONT_SMALL, max(labelX, (s32)x + 2), y + (CELL_HEIGHT - 12) / 2, 0, 0, sTextColors_Card, TEXT_SKIP_DRAW, label);
    if (copy)
        CopyWindowToVram(WIN_CARD, COPYWIN_GFX);
}

//------------------------------------------------------------------------------
// Flip
//------------------------------------------------------------------------------

static void StartFlip(u32 card, bool32 toBack, u32 nextState)
{
    u32 i;

    sMenu->flipToCard = card;
    sMenu->flipToBack = toBack;
    sMenu->stateAfterFlip = nextState;
    sMenu->flipHalfHeight = FLIP_HALF_HEIGHT;
    sMenu->flipGrowing = FALSE;
    sMenu->state = STATE_FLIP;

    for (i = 0; i < DISPLAY_HEIGHT; i++)
    {
        gScanlineEffectRegBuffers[0][i] = 0;
        gScanlineEffectRegBuffers[1][i] = 0;
    }
    sMenu->flipping = TRUE;
    SetHBlankCallback(HBlankCB_CardFlip);
    EnableInterrupts(INTR_FLAG_HBLANK);
    PlaySE(SE_RG_CARD_FLIP);
}

// Fills the scanline buffer so the card is drawn squashed to 2 * halfHeight
// lines around its middle row. Lines outside the card show BG row 0, which is
// always empty.
static void SetFlipScanlines(s32 halfHeight)
{
    s32 line, top = CARD_MID_Y - halfHeight, bottom = CARD_MID_Y + halfHeight;
    u16 *buffer = gScanlineEffectRegBuffers[0];

    for (line = 0; line < DISPLAY_HEIGHT; line++)
    {
        if (halfHeight <= 0 || line < top || line >= bottom)
            buffer[line] = -line;
        else
            buffer[line] = (CARD_MID_Y + ((line - CARD_MID_Y) * FLIP_HALF_HEIGHT) / halfHeight) - line;
    }
    SetGpuReg(REG_OFFSET_BLDY, (FLIP_HALF_HEIGHT - halfHeight) * 8 / FLIP_HALF_HEIGHT);
}

static void UpdateFlip(void)
{
    if (!sMenu->flipGrowing)
    {
        sMenu->flipHalfHeight -= FLIP_SHRINK_STEP;
        if (sMenu->flipHalfHeight <= 0)
        {
            sMenu->flipHalfHeight = 0;
            SetFlipScanlines(0);
            // Fully edge-on: swap to the other side.
            sMenu->card = sMenu->flipToCard;
            sMenu->onBack = sMenu->flipToBack;
            sMenu->state = sMenu->stateAfterFlip; // cell cursor is drawn for the new state
            DrawCard();
            sMenu->state = STATE_FLIP;
            MarkCardViewed();
            sMenu->flipGrowing = TRUE;
            PlaySE(SE_RG_CARD_FLIPPING);
            return;
        }
    }
    else
    {
        sMenu->flipHalfHeight += FLIP_GROW_STEP;
        if (sMenu->flipHalfHeight >= FLIP_HALF_HEIGHT)
        {
            sMenu->flipping = FALSE;
            SetHBlankCallback(NULL);
            DisableInterrupts(INTR_FLAG_HBLANK);
            SetGpuReg(REG_OFFSET_BG1VOFS, 0);
            SetGpuReg(REG_OFFSET_BG2VOFS, 0);
            SetGpuReg(REG_OFFSET_BLDY, 0);
            PlaySE(SE_RG_CARD_OPEN);
            if (sMenu->stateAfterFlip == STATE_BACK)
                EnterBackState();
            else
                EnterListState();
            return;
        }
    }
    SetFlipScanlines(sMenu->flipHalfHeight);
}

//------------------------------------------------------------------------------
// Input
//------------------------------------------------------------------------------

static void EnterListState(void)
{
    sMenu->state = STATE_LIST;
    PrintHelp(sMenu->cardCount > LIST_ROWS ? HELP_LIST_PAGED : HELP_LIST);
    PrintHint(sText_HintList);
}

static void EnterBackState(void)
{
    sMenu->state = STATE_BACK;
    PrintHelp(sMenu->cardCount > 1 ? HELP_BACK_MULTI : HELP_BACK);
    PrintHint(sText_HintBack);
}

static void Task_LeagueCards(u8 taskId)
{
    if (sMenu->redrawList && sMenu->listTaskId != TASK_NONE)
    {
        sMenu->redrawList = FALSE;
        RedrawListMenu(sMenu->listTaskId);
    }

    switch (sMenu->state)
    {
    case STATE_FADE_IN:
        if (!gPaletteFade.active)
        {
            if (sMenu->cardCount == 0)
                sMenu->state = STATE_EMPTY;
            else
                sMenu->state = STATE_LIST;
        }
        break;
    case STATE_LIST:
        HandleListInput(taskId);
        break;
    case STATE_BACK:
        HandleBackInput();
        break;
    case STATE_FLIP:
        UpdateFlip();
        break;
    case STATE_PRINT_FACT:
        if (!IsTextPrinterActiveOnWindow(WIN_MESSAGE))
            sMenu->state = STATE_WAIT_FACT;
        break;
    case STATE_WAIT_FACT:
        if (JOY_NEW(A_BUTTON | B_BUTTON))
        {
            PlaySE(SE_SELECT);
            DrawFactCell(sMenu->factCursor, TRUE);
            EnterBackState();
        }
        break;
    case STATE_EMPTY:
        if (JOY_NEW(B_BUTTON | A_BUTTON))
            StartClose();
        break;
    case STATE_FADE_OUT:
        if (!gPaletteFade.active)
        {
            DestroyTask(taskId);
            FreeLeagueCards();
        }
        break;
    }
}

static void HandleListInput(u8 taskId)
{
    s32 input = ListMenu_ProcessInput(sMenu->listTaskId);

    ListMenuGetScrollAndRow(sMenu->listTaskId, &sMenu->scrollOffset, &sMenu->selectedRow);
    switch (input)
    {
    case LIST_NOTHING_CHOSEN:
        break;
    case LIST_CANCEL:
        StartClose();
        break;
    default:
        sMenu->factCursor = 0;
        StartFlip(input, TRUE, STATE_BACK);
        break;
    }
}

// Moves the fact cursor one step in a direction on the grid. Returns FALSE if
// there is no fact that way.
static bool32 MoveFactCursor(s32 dx, s32 dy)
{
    s32 numFacts = sLeagueCards[sMenu->card].numFacts;
    s32 col = sMenu->factCursor % CELL_COLUMNS + dx;
    s32 row = sMenu->factCursor / CELL_COLUMNS + dy;
    s32 next;

    if (col < 0 || col >= CELL_COLUMNS || row < 0 || row >= CELL_ROWS)
        return FALSE;
    next = row * CELL_COLUMNS + col;
    if (next >= numFacts)
        return FALSE;
    sMenu->factCursor = next;
    return TRUE;
}

static void SwitchCard(s32 delta)
{
    s32 index = GetListIndex() + delta;

    if (index < 0)
        index = sMenu->cardCount - 1;
    else if (index >= sMenu->cardCount)
        index = 0;
    SetListIndex(index);
    sMenu->factCursor = 0;
    StartFlip(sMenu->listItems[index].id, TRUE, STATE_BACK);
}

static void HandleBackInput(void)
{
    u32 prevCursor = sMenu->factCursor;
    bool32 moved = FALSE;

    if (JOY_NEW(B_BUTTON))
    {
        StartFlip(sMenu->card, FALSE, STATE_LIST);
        return;
    }
    if (JOY_NEW(A_BUTTON))
    {
        if (sMenu->factCursor < sLeagueCards[sMenu->card].numFacts
         && LeagueCard_IsFactVisible(sMenu->card, sMenu->factCursor))
        {
            PlaySE(SE_SELECT);
            LeagueCard_MarkFactRead(sMenu->card, sMenu->factCursor);
            sMenu->redrawList = TRUE;
            PrintHelp(HELP_MESSAGE);
            PrintFact();
            sMenu->state = STATE_PRINT_FACT;
        }
        else
        {
            PlaySE(SE_FAILURE);
        }
        return;
    }
    if (sMenu->cardCount > 1 && JOY_NEW(L_BUTTON))
    {
        SwitchCard(-1);
        return;
    }
    if (sMenu->cardCount > 1 && JOY_NEW(R_BUTTON))
    {
        SwitchCard(1);
        return;
    }

    if (JOY_REPEAT(DPAD_UP))
        moved = MoveFactCursor(0, -1);
    else if (JOY_REPEAT(DPAD_DOWN))
        moved = MoveFactCursor(0, 1);
    else if (JOY_REPEAT(DPAD_LEFT))
        moved = MoveFactCursor(-1, 0);
    else if (JOY_REPEAT(DPAD_RIGHT))
        moved = MoveFactCursor(1, 0);

    if (moved)
    {
        PlaySE(SE_SELECT);
        DrawFactCell(prevCursor, FALSE);
        DrawFactCell(sMenu->factCursor, TRUE);
    }
}
