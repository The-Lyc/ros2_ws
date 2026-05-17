// 3 non-skippable + 1 skippable. NS: C1, C2, C3. S: C4.
// QoS periods follow paper Section VI.A (1.25 * native, same for every chain
// regardless of NS/S). NS vs S is encoded ONLY by priority tier (paper
// Section V-B): NS = priority 2, S = priority 1.
#define COMP_NAME    "3ns1s"
#define COMP_QOS_C1  100
#define COMP_QOS_C2  125
#define COMP_QOS_C3  200
#define COMP_QOS_C4  150
#define COMP_PRIO_C1 2
#define COMP_PRIO_C2 2
#define COMP_PRIO_C3 2
#define COMP_PRIO_C4 1
#define COMP_SKIP_C1 0
#define COMP_SKIP_C2 0
#define COMP_SKIP_C3 0
#define COMP_SKIP_C4 1
#include "composition_common.hpp"
int main(int argc, char ** argv) {return eva6_main(argc, argv);}
