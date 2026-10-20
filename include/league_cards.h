#ifndef GUARD_LEAGUE_CARDS_H
#define GUARD_LEAGUE_CARDS_H

#include "main.h"
#include "constants/league_cards.h"

struct LeagueFact
{
    const u8 *label; // Short entry on the back of the card.
    const u8 *text;  // Printed in the dialogue box when the fact is read.
};

struct LeagueCard
{
    const u8 *name;
    const u8 *title;
    u16 trainerPic;
    u8 numFacts;
    const struct LeagueFact *facts;
};

const struct LeagueCard *LeagueCard_GetInfo(u32 card);

bool32 LeagueCard_IsUnlocked(u32 card);
void LeagueCard_Unlock(u32 card);
void LeagueCard_Lock(u32 card);
bool32 LeagueCard_KnowsFact(u32 card, u32 fact);
bool32 LeagueCard_IsFactVisible(u32 card, u32 fact);
bool32 LeagueCard_LearnFact(u32 card, u32 fact);
void LeagueCard_ForgetFact(u32 card, u32 fact);
bool32 LeagueCard_IsFactUnread(u32 card, u32 fact);
void LeagueCard_MarkFactRead(u32 card, u32 fact);
bool32 LeagueCard_IsUnread(u32 card);
u32 LeagueCard_CountUnlocked(void);
u32 LeagueCard_CountKnownFacts(u32 card);

void LeagueCards_Open(MainCallback exitCallback);
void CB2_OpenLeagueCardsFromBag(void);

#endif // GUARD_LEAGUE_CARDS_H
