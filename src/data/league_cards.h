// League Card contents, indexed by LEAGUE_CARD_* (include/constants/league_cards.h).
//
// Text limits (checked by localization/rules.json and test/league_cards.c):
//   name   FONT_NORMAL, 54px  (list on the left and card front)
//   title  FONT_SMALL,  54px per line, 2 lines (card front)
//   label  FONT_SMALL,  56px  (one cell on the card back)
//   text   FONT_NORMAL, 208px per line, 2 lines per page (dialogue box).
//          Use \n for a new line and \p for a new page.

static const struct LeagueFact sCollieFacts[] =
{
    {
        .label = COMPOUND_STRING("Hometown"),
        .text = COMPOUND_STRING(
            "COLLIE grew up on a sheep farm and\n"
            "still visits home every harvest."),
    },
    {
        .label = COMPOUND_STRING("Partner"),
        .text = COMPOUND_STRING(
            "COLLIE's first partner was a\n"
            "GROWLITHE that helped herd the flock."),
    },
    {
        .label = COMPOUND_STRING("Battling"),
        .text = COMPOUND_STRING(
            "COLLIE likes to wear foes down\n"
            "with steady, patient pressure.\p"
            "COLLIE says a good battle is like\n"
            "a long day of honest work."),
    },
    {
        .label = COMPOUND_STRING("Rumor"),
        .text = COMPOUND_STRING(
            "People say COLLIE can whistle\n"
            "loud enough to be heard a town away."),
    },
};

static const struct LeagueFact sSpruceFacts[] =
{
    {
        .label = COMPOUND_STRING("Hometown"),
        .text = COMPOUND_STRING(
            "SPRUCE was raised in a cabin deep\n"
            "in the northern pine forest."),
    },
    {
        .label = COMPOUND_STRING("Hobby"),
        .text = COMPOUND_STRING(
            "SPRUCE carves small wooden POKéMON\n"
            "figures and gives them to children."),
    },
    {
        .label = COMPOUND_STRING("Battling"),
        .text = COMPOUND_STRING(
            "SPRUCE favors sturdy POKéMON that\n"
            "can take a hit and stand their ground."),
    },
    {
        .label = COMPOUND_STRING("Favorite"),
        .text = COMPOUND_STRING(
            "SPRUCE swears that nothing beats\n"
            "berries cooked over a campfire."),
    },
    {
        .label = COMPOUND_STRING("Goal"),
        .text = COMPOUND_STRING(
            "SPRUCE hopes to plant a tree for\n"
            "every challenger who wins a match."),
    },
    {
        .label = COMPOUND_STRING("Secret"),
        .text = COMPOUND_STRING(
            "For all that size, SPRUCE is said to\n"
            "be terrified of thunderstorms."),
    },
};

static const struct LeagueCard sLeagueCards[LEAGUE_CARD_COUNT] =
{
    [LEAGUE_CARD_COLLIE] =
    {
        .name = COMPOUND_STRING("COLLIE"),
        .title = COMPOUND_STRING("Shepherd of\nthe League"),
        .trainerPic = TRAINER_PIC_COOLTRAINER_F,
        .numFacts = ARRAY_COUNT(sCollieFacts),
        .facts = sCollieFacts,
    },
    [LEAGUE_CARD_SPRUCE] =
    {
        .name = COMPOUND_STRING("SPRUCE"),
        .title = COMPOUND_STRING("Evergreen\nGuardian"),
        .trainerPic = TRAINER_PIC_HIKER,
        .numFacts = ARRAY_COUNT(sSpruceFacts),
        .facts = sSpruceFacts,
    },
};
