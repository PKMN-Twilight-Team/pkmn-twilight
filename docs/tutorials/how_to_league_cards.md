# League Cards

League Cards combine the Sword/Shield League Cards with the FRLG Fame Checker.
The player keeps them in the **Card Case** key item (`ITEM_CARD_CASE`). Opening it shows:

* a list of unlocked trainers on the left. Only unlocked cards appear, so later characters stay hidden.
* that trainer's card on the right. The front has the portrait, name, title and how many facts are known.
* the back of the card (flip it with A), with up to six facts. Selecting a known fact prints its text in the dialogue box.

A red `!` next to a trainer marks an unread card: either it was never looked at,
or it has a fact the player hasn't opened yet. On the back of the card, unread
facts are highlighted.

## Controls

| Where | Button | Action |
|---|---|---|
| List | D-pad Up/Down | Choose a trainer |
| List | L/R | Page through the list (shown when there are more than 5 cards) |
| List | A | Flip the card to its back |
| List | B | Close the Card Case |
| Card back | D-pad | Move between facts |
| Card back | A | Read the selected fact (locked facts play an error sound) |
| Card back | L/R | Previous/next trainer, flipping to that card's back |
| Card back | B | Flip back to the front |
| Fact text | A/B | Close the text |

The header always shows the buttons available in the current state.

## Adding a card

1. Add a constant to `include/constants/league_cards.h` before `LEAGUE_CARD_COUNT`.
   The list is ordered by this enum. Up to `LEAGUE_CARD_SAVE_SLOTS` (32) cards fit in
   the save without changing its layout.
2. Add the facts and the card to `src/data/league_cards.h`:

```c
static const struct LeagueFact sBlazeFacts[] =
{
    {
        .label = COMPOUND_STRING("Hometown"),
        .text = COMPOUND_STRING(
            "BLAZE comes from a village at\n"
            "the foot of the volcano."),
    },
    // ... up to LEAGUE_CARD_MAX_FACTS (6) facts
};

    [LEAGUE_CARD_BLAZE] =
    {
        .name = COMPOUND_STRING("BLAZE"),
        .title = COMPOUND_STRING("Fire Gym\nLeader"),
        .trainerPic = TRAINER_PIC_LEADER_FLANNERY,
        .numFacts = ARRAY_COUNT(sBlazeFacts),
        .facts = sBlazeFacts,
    },
```

Fact numbers in scripts are the positions in this array (0 to 5). The back of the
card shows them as two columns, left to right then top to bottom.

### Text limits

| Field | Font | Limit |
|---|---|---|
| `name` | `FONT_NORMAL` | 54px, 1 line |
| `title` | `FONT_SMALL` | 54px per line, 2 lines |
| `label` | `FONT_SMALL` | 56px, 1 line |
| `text` | `FONT_NORMAL` | 208px per line, 2 lines per page; use `\p` for more pages |

`make check TESTS="League Card"` checks the English text against these limits.
`localization/rules.json` applies the same limits to translations, so `make loc-validate`
reports a translation that is too long. After adding or changing text, run
`make loc-extract` and commit the updated CSVs in `localization/text/`.

## Scripting

`card` can be a `LEAGUE_CARD_*` constant or a var. `fact` can be a number or a var.

| Command | Effect |
|---|---|
| `unlockleaguecard card` | Adds the card to the Card Case. Facts learned earlier become visible, and the card is marked unread. |
| `lockleaguecard card` | Removes the card from the Card Case. Learned facts are kept. |
| `learnleaguefact card, fact` | Reveals a fact. If the card is still locked, the fact is stored silently and appears once the card is unlocked. Sets `VAR_RESULT` to `TRUE` if the fact is new and visible right now. |
| `forgetleaguefact card, fact` | Hides a fact again. |
| `checkleaguecard card` | `VAR_RESULT` = whether the card is unlocked. |
| `checkleaguefact card, fact` | `VAR_RESULT` = whether the fact has been learned, even if the card is still locked. |
| `openleaguecards` | Opens the Card Case. The script continues after it closes. |

Shortcuts: `goto_if_league_card_unlocked`, `goto_if_league_card_locked`,
`goto_if_league_fact_known` and `goto_if_league_fact_unknown`.

```
MyTown_EventScript_Gossip::
    msgbox MyTown_Text_HeardAboutCollie, MSGBOX_NPC
    learnleaguefact LEAGUE_CARD_COLLIE, 3
    goto_if_eq VAR_RESULT, FALSE, MyTown_EventScript_GossipEnd
    msgbox MyTown_Text_CardCaseUpdated, MSGBOX_DEFAULT
MyTown_EventScript_GossipEnd:
    end
```

To give the player the Card Case, use `giveitem ITEM_CARD_CASE`.

From C, use the functions in `include/league_cards.h`, such as `LeagueCard_Unlock`,
`LeagueCard_LearnFact` and `LeagueCard_IsUnread`.

## Save data

`gSaveBlock1Ptr->leagueCards[LEAGUE_CARD_SAVE_SLOTS]` is at the end of `SaveBlock1` and
is indexed by card ID. Each entry is 2 bytes:

* `knownFacts`: one bit per fact the player has learned
* `readFacts`: one bit per fact the player has opened
* `unlocked`
* `viewed`: the card has been looked at since it was unlocked

## Adding art

Until there is art, the screen is drawn in solid colours from the palettes in
`src/league_cards.c`. Trainer portraits use the normal trainer front pics. To use
real art:

1. Add these files to `graphics/league_cards/`:

| File | Contents |
|---|---|
| `bg.png` + `bg.pal` | Background tiles, 16 colours. The first colour is the backdrop. |
| `bg.bin` | 32x32 tilemap for the background (palette 0), e.g. made with Tilemap Studio |
| `card_front.png` | Front of the card, 136x96 |
| `card_back.png` | Back of the card, 136x96 |
| `card.pal` | Palette shared by both card images |

The card images sit behind the text and fact cells, so leave room for them.
On the front, the name, title and fact count are at the left and the 64x64 portrait
is at x=64, y=16. On the back, there is a 2x3 grid of fact cells.

The card palette is also used to draw text and the fact cells, so keep these
indices:

| Index | Use |
|---|---|
| 0 | transparent |
| 1 | white text |
| 2 | dark text |
| 3 | text shadow |
| 7 | fact cell border |
| 8 | known fact |
| 9 | unread fact |
| 10 | locked fact |
| 11 | selected fact border |
| 12 | title text |

2. Create `src/data/league_cards_graphics.h`:

```c
// Art for the Card Case screen. Only used when LEAGUE_CARDS_CUSTOM_GFX is TRUE.
static const u32 gLeagueCardsBg_Gfx[] = INCBIN_U32("graphics/league_cards/bg.4bpp");
static const u32 gLeagueCardsBg_Tilemap[] = INCBIN_U32("graphics/league_cards/bg.bin");
static const u16 gLeagueCardsBg_Pal[] = INCBIN_U16("graphics/league_cards/bg.gbapal");
static const u32 gLeagueCards_FrontGfx[] = INCBIN_U32("graphics/league_cards/card_front.4bpp");
static const u32 gLeagueCards_BackGfx[] = INCBIN_U32("graphics/league_cards/card_back.4bpp");
static const u16 gLeagueCards_CardPal[] = INCBIN_U16("graphics/league_cards/card.gbapal");
```

3. Set `#define LEAGUE_CARDS_CUSTOM_GFX TRUE` at the top of `src/league_cards.c`.

The Card Case uses the Fame Checker's item icon until it has its own: change
`.iconPic` and `.iconPalette` under `[ITEM_CARD_CASE]` in `src/data/items.h`.
