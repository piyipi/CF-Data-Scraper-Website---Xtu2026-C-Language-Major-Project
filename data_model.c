#include "data_model.h"
#include <stdlib.h>

void userdata_free(UserData *ud)
{
    if (!ud) return;
    free(ud->records);
    ud->records = NULL;
    ud->record_count = 0;
}
