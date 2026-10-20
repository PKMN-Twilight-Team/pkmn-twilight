#ifndef GUARD_CONSTANTS_LEAGUE_CARDS_H
#define GUARD_CONSTANTS_LEAGUE_CARDS_H

// Cards appear in the Card Case list in this order.
// Add new cards just before LEAGUE_CARD_COUNT.
enum LeagueCardId
{
    LEAGUE_CARD_NONE,
    LEAGUE_CARD_COLLIE,
    LEAGUE_CARD_SPRUCE,
    LEAGUE_CARD_COUNT
};

// Each card has at most this many facts on its back.
#define LEAGUE_CARD_MAX_FACTS 6

// Number of cards reserved in SaveBlock1. Raising LEAGUE_CARD_COUNT up to
// this value does not change the save layout.
#define LEAGUE_CARD_SAVE_SLOTS 32

#endif // GUARD_CONSTANTS_LEAGUE_CARDS_H
