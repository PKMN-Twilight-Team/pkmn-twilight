struct LeagueFact {
    u16 flag;
    const u8 *text;
    const u8 *source;
};

struct LeagueCard {
    const u8 *name;
    const u8 *title;
    const u8 *bio;
    u16 unlockFlag;
    u16 trainerPic;
    u8 numFacts;
    const struct LeagueFact *facts;
}