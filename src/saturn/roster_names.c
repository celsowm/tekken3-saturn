/* Fighter names by roster id, shared by the fight and menu programs. */
enum { ROSTER_COUNT=21 };

static const char* const g_names[ROSTER_COUNT]={
    "PAUL","LAW","LEI","KING","YOSHIMITSU","NINA","HWOARANG","XIAOYU","EDDY","JIN",
    "JULIA","KUMA","BRYAN","HEIHACHI","OGRE","MOKUJIN","GUN JACK","GON","ANNA","DOCTOR B","TRUE OGRE"};

const char* t3_fighter_name(unsigned id) {return id<ROSTER_COUNT?g_names[id]:"";}
