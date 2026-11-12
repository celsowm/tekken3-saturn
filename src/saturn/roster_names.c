/* Fighter names by roster id, shared by the fight and menu programs. */
/* The final four IDs are Force render models, outside the selectable roster. */
enum { NAMED_FIGHTERS=25 };

static const char* const g_names[NAMED_FIGHTERS]={
    "PAUL","LAW","LEI","KING","YOSHIMITSU","NINA","HWOARANG","XIAOYU","EDDY","JIN",
    "JULIA","KUMA","BRYAN","HEIHACHI","OGRE","MOKUJIN","GUN JACK","GON","ANNA","DOCTOR B","TRUE OGRE",
    "CROW","FALCON","HAWK","OWL"};

const char* t3_fighter_name(unsigned id) {return id<NAMED_FIGHTERS?g_names[id]:"";}
