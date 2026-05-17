// 2 non-skippable + 2 skippable. NS: C1, C2. S: C3, C4.
// QoS values same across all compositions (paper Section VI.A).
#define COMP_NAME    "2ns2s"
#define COMP_QOS_C1  100
#define COMP_QOS_C2  125
#define COMP_QOS_C3  200
#define COMP_QOS_C4  150
#define COMP_PRIO_C1 2
#define COMP_PRIO_C2 2
#define COMP_PRIO_C3 1
#define COMP_PRIO_C4 1
#define COMP_SKIP_C1 0
#define COMP_SKIP_C2 0
#define COMP_SKIP_C3 1
#define COMP_SKIP_C4 1
#include "composition_common.hpp"
int main(int argc, char ** argv) {return eva6_main(argc, argv);}
