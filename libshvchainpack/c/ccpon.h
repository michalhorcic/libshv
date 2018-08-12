#ifndef C_CPON_H
#define C_CPON_H

#include "ccpcp.h"

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

time_t ccpon_timegm(struct tm *tm);
void ccpon_gmtime(int64_t epoch_sec, struct tm *tm);

/******************************* P A C K **********************************/

void ccpon_pack_null (ccpcp_pack_context* pack_context);
void ccpon_pack_boolean (ccpcp_pack_context* pack_context, bool b);
void ccpon_pack_int (ccpcp_pack_context* pack_context, int64_t i);
void ccpon_pack_uint (ccpcp_pack_context* pack_context, uint64_t i);
void ccpon_pack_double (ccpcp_pack_context* pack_context, double d);
void ccpon_pack_decimal (ccpcp_pack_context* pack_context, int64_t i, int dec_places);
void ccpon_pack_str (ccpcp_pack_context* pack_context, const char* s, unsigned l);
//void ccpon_pack_blob (ccpcp_pack_context* pack_context, const void *v, unsigned l);
void ccpon_pack_date_time (ccpcp_pack_context* pack_context, int64_t epoch_msecs, int min_from_utc);

void ccpon_pack_array_begin (ccpcp_pack_context* pack_context, int size);
void ccpon_pack_array_end (ccpcp_pack_context* pack_context);

void ccpon_pack_list_begin (ccpcp_pack_context* pack_context);
void ccpon_pack_list_end (ccpcp_pack_context* pack_context);

void ccpon_pack_map_begin (ccpcp_pack_context* pack_context);
void ccpon_pack_map_end (ccpcp_pack_context* pack_context);

void ccpon_pack_imap_begin (ccpcp_pack_context* pack_context);
void ccpon_pack_imap_end (ccpcp_pack_context* pack_context);

void ccpon_pack_meta_begin (ccpcp_pack_context* pack_context);
void ccpon_pack_meta_end (ccpcp_pack_context* pack_context);

void ccpon_pack_copy_str (ccpcp_pack_context* pack_context, const void *str);

void ccpon_pack_field_delim (ccpcp_pack_context* pack_context, bool is_first_field);
void ccpon_pack_key_delim (ccpcp_pack_context* pack_context);

/***************************** U N P A C K ********************************/
void ccpcp_unpack_context_init(ccpcp_unpack_context* unpack_context, uint8_t* data, size_t length, ccpcp_unpack_underflow_handler huu);

void ccpon_unpack_next (ccpcp_unpack_context* unpack_context);
void ccpon_skip_items (ccpcp_unpack_context* unpack_context, long item_count);

#ifdef __cplusplus
}
#endif

#endif /* C_CPON_H */
