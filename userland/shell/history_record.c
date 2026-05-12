#include "fl/history_record.h"
#include <stdio.h>
#include <string.h>

size_t fl_history_record_pack(char *out, size_t out_cap, uint8_t bundle_rev,
                              fl_contract_surface_t surface, fl_result_t last_rc,
                              const char *cmd) {
    if (!out || out_cap < 16u || !cmd)
        return (size_t)-1;

    size_t clen = strlen(cmd);
    if (memchr(cmd, FL_HISTORY_RECORD_SEP, clen) != NULL)
        return (size_t)-1;
    if (memchr(cmd, '\n', clen) != NULL)
        return (size_t)-1;

    char json[112];
    int jn =
        snprintf(json, sizeof json, "{\"br\":%u,\"sf\":%u,\"rc\":%d}",
                 (unsigned)bundle_rev, (unsigned)surface, (int)last_rc);
    if (jn <= 0 || (size_t)jn >= sizeof json)
        return (size_t)-1;

    size_t need = 3u + 1u + (size_t)jn + 1u + clen + 1u;
    if (need > out_cap)
        return (size_t)-1;

    unsigned char *w = (unsigned char *)out;
    memcpy(w, FL_HISTORY_RECORD_TAG, 3u);
    w += 3;
    *w++ = (unsigned char)FL_HISTORY_RECORD_SEP;
    memcpy(w, json, (size_t)jn);
    w += (size_t)jn;
    *w++ = (unsigned char)FL_HISTORY_RECORD_SEP;
    memcpy(w, cmd, clen);
    w += clen;
    *w++ = (unsigned char)'\n';
    return (size_t)(w - (unsigned char *)out);
}

int fl_history_record_unpack_cmd(const char *stored_line, char *cmd_out,
                                 size_t cmd_out_cap, uint8_t *bundle_rev_out,
                                 fl_contract_surface_t *surface_out,
                                 fl_result_t *rc_out) {
    if (!stored_line || !cmd_out || cmd_out_cap == 0)
        return 0;

    if (strncmp(stored_line, FL_HISTORY_RECORD_TAG, 3) != 0 ||
        stored_line[3] != FL_HISTORY_RECORD_SEP) {
        size_t cap = cmd_out_cap - 1u;
        size_t n = strlen(stored_line);
        if (n > cap)
            n = cap;
        memcpy(cmd_out, stored_line, n);
        cmd_out[n] = '\0';
        return 0;
    }

    const char *json_start = stored_line + 4u;
    const char *sep2 = strchr(json_start, FL_HISTORY_RECORD_SEP);
    if (!sep2) {
        size_t cap = cmd_out_cap - 1u;
        size_t n = strlen(stored_line);
        if (n > cap)
            n = cap;
        memcpy(cmd_out, stored_line, n);
        cmd_out[n] = '\0';
        return 0;
    }

    char jsonbuf[128];
    size_t jsonlen = (size_t)(sep2 - json_start);
    if (jsonlen >= sizeof jsonbuf)
        jsonlen = sizeof jsonbuf - 1u;
    memcpy(jsonbuf, json_start, jsonlen);
    jsonbuf[jsonlen] = '\0';

    unsigned br_u = 0, sf_u = 0;
    int rc_i = 0;
    if (sscanf(jsonbuf, "{\"br\":%u,\"sf\":%u,\"rc\":%d}", &br_u, &sf_u,
               &rc_i) != 3) {
        br_u = 0;
        sf_u = 0;
        rc_i = 0;
    }

    const char *cmd = sep2 + 1u;
    size_t clen = strlen(cmd);
    while (clen > 0 && (cmd[clen - 1] == '\n' || cmd[clen - 1] == '\r'))
        clen--;
    if (clen >= cmd_out_cap)
        clen = cmd_out_cap - 1u;
    memcpy(cmd_out, cmd, clen);
    cmd_out[clen] = '\0';

    if (bundle_rev_out)
        *bundle_rev_out = (uint8_t)br_u;
    if (surface_out)
        *surface_out = (fl_contract_surface_t)sf_u;
    if (rc_out)
        *rc_out = (fl_result_t)rc_i;
    return 1;
}
