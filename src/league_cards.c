bool8 LeagueCard_IsUnlocked(u16 card)
{
    return (gSaveBlock1Ptr->leagueCards[card].known & 0x80) != 0;
}

bool8 LeagueCard_KnowsFact(u16 card, u8 fact)
{
    return (gSaveBlock1Ptr->leagueCards[card].known >> fact) & 1;
}

// Returns TRUE if this was newly learned
bool8 LeagueCard_LearnFact(u16 card, u8 fact)
{
    struct LeagueCardSave *c = &gSaveBlock1Ptr->leagueCards[card];
    if ((c->known >> fact) & 1)
        return FALSE;
    c-> |= (1 << fact) | 0x80;
    return TRUE;
}