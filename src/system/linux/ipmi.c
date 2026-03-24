#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/ipmi.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <poll.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <uv.h>
#include "parsers/ipmi.h"
#include "common/logs.h"
#include "main.h"
extern aconf *ac;

#define IPMI_NETFN_CHASSIS    0x00
#define IPMI_NETFN_SENSOR     0x04
#define IPMI_NETFN_STORAGE    0x0a
#define IPMI_NETFN_TRANSPORT  0x0c
#define IPMI_NETFN_DCMI       0x2c

struct ipmi_ctx {
    int fd;
};

typedef struct sensor_map {
	uint8_t num;
	char name[32];
} sensor_map;

int ipmi_cmd(struct ipmi_ctx *ctx, uint8_t netfn, uint8_t cmd, uint8_t *data, size_t dlen, uint8_t *rsp, size_t *rlen) {
    struct ipmi_system_interface_addr addr = {0};
    struct ipmi_msg msg = {0};
    struct ipmi_req req = {0};
    addr.addr_type = IPMI_SYSTEM_INTERFACE_ADDR_TYPE;
    addr.channel = IPMI_BMC_CHANNEL;
    msg.netfn = netfn;
    msg.cmd = cmd;
    msg.data = data;
    msg.data_len = dlen;
    req.addr = (unsigned char*)&addr;
    req.addr_len = sizeof(addr);
    req.msgid = 0;
    req.msg = msg;

    if (ioctl(ctx->fd, IPMICTL_SEND_COMMAND, &req) < 0) return -1;

    struct ipmi_recv recv = {0};
    struct ipmi_addr raddr = {0};
    recv.addr = (unsigned char*)&raddr;
    recv.addr_len = sizeof(raddr);
    recv.msg.data = rsp;
    recv.msg.data_len = *rlen;
    uv_loop_t *loop = uv_default_loop();
    uv_update_time(loop);
    uint64_t start_ms = uv_now(loop);
    const uint64_t timeout_ms = 2000;

    while (1) {
        if (ioctl(ctx->fd, IPMICTL_RECEIVE_MSG_TRUNC, &recv) == 0)
            break;

        if (errno != EAGAIN && errno != EWOULDBLOCK)
            return -1;

        uv_update_time(loop);
        if ((uv_now(loop) - start_ms) >= timeout_ms) {
            carglog(ac->system_carg, L_ERROR, "IPMI receive timeout");
            return -1;
        }

        uv_sleep(5);
    }

    *rlen = recv.msg.data_len;
    return rsp[0];
}

void do_chassis_status(struct ipmi_ctx *ctx) {
    uint8_t rsp[32] = {0};
    size_t rlen = sizeof(rsp);

    /* Need at least completion code + 3 data bytes (rsp[0..3]). */
    if (ipmi_cmd(ctx, 0x00, 0x01, NULL, 0, rsp, &rlen) == 0 && rlen >= 4) {
        uint64_t val = (rsp[1] & 0x01) ? 1 : 0;
        metric_add_auto("IPMI_System_Power", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[1] & 0x02) ? 1 : 0;
        metric_add_auto("IPMI_Power_Overload", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[1] & 0x04) ? 1 : 0;
        metric_add_auto("IPMI_Power_Interlock", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[1] & 0x08) ? 1 : 0;
        metric_add_auto("IPMI_Main_Power_Fault", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[1] & 0x10) ? 1 : 0;
        metric_add_auto("IPMI_Power_Control_Fault", &val, DATATYPE_UINT, ac->system_carg);

        val = (rsp[2] & 0x01) ? 1 : 0;
        metric_add_auto("IPMI_chassis_event_ac_failed", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[2] & 0x02) ? 1 : 0;
        metric_add_auto("IPMI_chassis_event_overload", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[2] & 0x04) ? 1 : 0;
        metric_add_auto("IPMI_chassis_event_interlock", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[2] & 0x08) ? 1 : 0;
        metric_add_auto("IPMI_chassis_event_fault_main", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[2] & 0x10) ? 1 : 0;
        metric_add_auto("IPMI_chassis_event_power_on_via_ipmi", &val, DATATYPE_UINT, ac->system_carg);
        
        val = (rsp[3] & 0x01) ? 1 : 0;
        metric_add_auto("IPMI_Chassis_Intrusion", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[3] & 0x02) ? 1 : 0;
        metric_add_auto("IPMI_Front_Panel_Lockout", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[3] & 0x04) ? 1 : 0;
        metric_add_auto("IPMI_Drive_Fault", &val, DATATYPE_UINT, ac->system_carg);
        val = (rsp[3] & 0x08) ? 1 : 0;
        metric_add_auto("IPMI_Cooling_Fan_Fault", &val, DATATYPE_UINT, ac->system_carg);
        
        const char *last_event = "none";
        if      (rsp[2] & 0x01) last_event = "ac-failed";
        else if (rsp[2] & 0x02) last_event = "overload";
        else if (rsp[2] & 0x04) last_event = "interlock";
        else if (rsp[2] & 0x08) last_event = "fault";
        else if (rsp[2] & 0x10) last_event = "command-via-ipmi";
        //printf("IPMI_Last_Power_Event {state=\"%s\"} 1\n", last_event);
        metric_add_auto("IPMI_Last_Power_Event", &last_event, DATATYPE_STRING, ac->system_carg);

        uint8_t policy_bits = (rsp[1] >> 5) & 0x03;
        const char *policy_str = "unknown";

        switch (policy_bits) {
            case 0: policy_str = "always-off"; break;
            case 1: policy_str = "previous";   break;
            case 2: policy_str = "always-on";   break;
        }
        //printf("IPMI_Power_Restore_Policy {state=\"%s\"} 1\n", policy_str);
        metric_add_auto("IPMI_Power_Restore_Policy", &policy_str, DATATYPE_STRING, ac->system_carg);
    }
}

double scale_power(uint16_t raw, uint8_t unit_byte) {
    if (unit_byte == 0x40) return (double)raw / 1000.0;      // Милливатты -> Ватты
    if (unit_byte == 0x02) return (double)raw / 1000000.0;   // Микроватты -> Ватты
    return (double)raw;                                      // По умолчанию Ватты (0x01)
}

void get_dcmi_power_metrics(struct ipmi_ctx *ctx) {
    uint8_t req[] = { 0xDC, 0x01, 0x00, 0x00 };
    uint8_t rsp[32] = {0};
    size_t rlen = sizeof(rsp);

    /* We read up to rsp[17], so require at least 18 bytes in the response. */
    if (ipmi_cmd(ctx, 0x2C, 0x02, req, 4, rsp, &rlen) == 0 && rlen >= 18 && rsp[1] == 0xDC) {

        uint8_t unit = rsp[17];

        double current = scale_power(rsp[4]  | (rsp[5] << 8),  unit);
        double min     = scale_power(rsp[6]  | (rsp[7] << 8),  unit);
        double max     = scale_power(rsp[8]  | (rsp[9] << 8),  unit);
        double avg     = scale_power(rsp[10] | (rsp[11] << 8), unit) / 100;

        uint64_t state = (rsp[16] != 0) ? 1 : 0;

        metric_add_auto("IPMI_dcmi_power_reading_instantaneous", &current, DATATYPE_DOUBLE, ac->system_carg);
        metric_add_auto("IPMI_dcmi_power_reading_minimum", &min, DATATYPE_DOUBLE, ac->system_carg);
        metric_add_auto("IPMI_dcmi_power_reading_maximum", &max, DATATYPE_DOUBLE, ac->system_carg);
        metric_add_auto("IPMI_dcmi_power_reading_average_over_sample_period", &avg, DATATYPE_DOUBLE, ac->system_carg);
        metric_add_auto("IPMI_dcmi_power_reading_state", &state, DATATYPE_UINT, ac->system_carg);
    }
}


void do_prom_lan_print(struct ipmi_ctx *ctx) {
    uint8_t params[] = {3, 6, 12, 13, 14, 15, 4, 5};
    char *labels[] = { "ip", "mask", "default_gateway_ip", "default_gateway_mac", "backup_gateway_ip", "backup_gateway_mac", "source", "mac" };

    // printf("IPMI_Lan {");
    alligator_ht *hash = alligator_ht_init(NULL);

    for (int i = 0; i < 8; i++) {
        uint8_t req[] = { 0x01, params[i], 0x00, 0x00 };
        uint8_t rsp[32] = {0};
        size_t rlen = sizeof(rsp);

        if (ipmi_cmd(ctx, 0x0c, 0x02, req, 4, rsp, &rlen) == 0) {
            if (params[i] == 4) {
                if (rlen < 3)
                    continue;
                int src = rsp[2] & 0x0F;
                char *src_str = (src == 1) ? "static" : (src == 2) ? "dhcp" : "unknown";
                //printf("%s=\"%s\"", labels[i], src_str);
                labels_hash_insert_nocache(hash, labels[i], src_str);
            }
            else if (params[i] == 5 || params[i] == 13 || params[i] == 15) {
                if (rlen < 8)
                    continue;
                //printf("%s=\"%02x:%02x:%02x:%02x:%02x:%02x\"", labels[i], rsp[2], rsp[3], rsp[4], rsp[5], rsp[6], rsp[7]);
                char src_str[64];
                snprintf(src_str, 63, "%02x:%02x:%02x:%02x:%02x:%02x", rsp[2], rsp[3], rsp[4], rsp[5], rsp[6], rsp[7]);
                labels_hash_insert_nocache(hash, labels[i], src_str);
            }
            else {
                if (rlen < 6)
                    continue;
                //printf("%s=\"%d.%d.%d.%d\"", labels[i], rsp[2], rsp[3], rsp[4], rsp[5]);
                char src_str[64];
                snprintf(src_str, 63, "%d.%d.%d.%d", rsp[2], rsp[3], rsp[4], rsp[5]);
                labels_hash_insert_nocache(hash, labels[i], src_str);
            }

            //if (i < 7) printf(", ");
        }
    }
    //printf("} 1\n");
    uint64_t val = 1;
    metric_add("IPMI_Lan", hash, &val, DATATYPE_UINT, ac->system_carg);
}

int16_t get_10bit(uint8_t low, uint8_t high) {
    uint16_t val = ((high & 0xC0) << 2) | low;
    if (val > 511) return (int16_t)(val - 1024);
    return (int16_t)val;
}

char* get_unit_str(uint8_t unit_code) {
    static char* units[] = {
        "unspecified", "degrees C", "degrees F", "Kelvins", "Volts", "Amps", "Watts",
        "Joules", "Coulombs", "VA", "Nits", "Lumen", "Lux", "Candela", "kPa", "PSI",
        "Newton", "CFM", "RPM", "Hz", "microsecond", "millisecond", "second", "minute",
        "hour", "day", "week", "mil", "inches", "feet", "cu in", "cu feet", "mm",
        "cm", "m", "sq in", "sq feet", "sq mm", "sq cm", "sq m", "liters", "quarts",
        "gallons", "grams", "kg", "lbs", "oz", "force-lb", "cart-lb", "ft-lb",
        "in-lb", "fl oz", "radians", "steradians", "revolutions", "cycles", "gravity",
        "ounce", "ounce-in", "gauss", "gilberts", "henry", "millihenry", "farad",
        "microfarad", "ohms", "siemens", "mole", "becquerel", "PPM", "reserved",
        "Decibels", "DbA", "DbC", "gray", "sievert", "color temp deg K", "bit",
        "kilobit", "megabit", "gigabit", "byte", "kilobyte", "megabyte", "gigabyte",
        "word", "dword", "qword", "line", "hit", "miss", "retry", "reset",
        "overflow", "underrun", "collision", "packets", "messages", "characters",
        "error", "correctable err", "uncorrectable err"
    };
    if (unit_code < (sizeof(units) / sizeof(char*))) return units[unit_code];
    return "units";
}

double calc_val(uint8_t raw, int16_t m, int16_t b, int8_t k1, int8_t k2) {
    double b_term = (double)b * pow(10, k1);
    return (double)(m * raw + b_term) * pow(10, k2);
}

void ipmi_set_metric(char *metric_name, uint8_t *trsp, uint8_t index, int16_t m, int16_t b, int8_t k1, int8_t k2, char *name, char *unit_str) {
    double thval = calc_val(trsp[index], m, b, k1, k2);
    //printf("%s {name=\"%s\", measure=\"%s\"} %lf\n", metric_name, name, unit_str, thval);
    metric_add_labels2(metric_name, &thval, DATATYPE_DOUBLE, ac->system_carg, "name", name, "measure", unit_str);
}

void do_sensor_list(struct ipmi_ctx *ctx) {
    uint16_t next = 0;
    uint8_t res_d[10] = {0};
    size_t res_l;

    while (next != 0xFFFF) {
        res_l = sizeof(res_d);
        if (ipmi_cmd(ctx, 0x0a, 0x22, NULL, 0, res_d, &res_l) != 0) break;
        if (res_l < 3) break;

        uint8_t req[] = { res_d[1], res_d[2], next & 0xFF, next >> 8, 0x00, 0xFF };
        uint8_t rsp[256] = {0};
        size_t rlen = sizeof(rsp);
        if (ipmi_cmd(ctx, 0x0a, 0x23, req, 6, rsp, &rlen) != 0) break;
        if (rlen < 6) break;

        next = rsp[1] | (rsp[2] << 8);
        uint8_t *sdr = &rsp[3];
        size_t sdr_len = rlen - 3;
        if (sdr_len < 49)
            goto next_record;
        if (sdr[3] != 0x01) continue;

        uint8_t s_num = sdr[7];

        int16_t m = (int16_t)sdr[24] | (int16_t)((sdr[25] & 0xC0) << 2);
        if (m > 511) m -= 1024;
        int16_t b = (int16_t)sdr[26] | (int16_t)((sdr[27] & 0xC0) << 2);
        if (b > 511) b -= 1024;
        int8_t k1 = sdr[28] & 0x0F;
        if (k1 > 7) k1 -= 16;
        int8_t k2 = (sdr[28] >> 4) & 0x0F;
        if (k2 > 7) k2 -= 16;

        char name[17] = {0};
        int nlen = sdr[47] & 0x1F;
        if (nlen > 16) nlen = 16;
        if (48 + nlen > (int)sdr_len) nlen = (int)sdr_len - 48;
        if (nlen < 0) nlen = 0;
        memcpy(name, &sdr[48], (size_t)nlen);
        char* unit_str = get_unit_str(sdr[21]);

        if (strcmp(unit_str, "Volts") == 0 && k2 == 0) {
            k2 = -3;
        }

	    double val = 0;
        uint8_t srsp[32] = {0}; size_t srlen = sizeof(srsp);
        if (ipmi_cmd(ctx, 0x04, 0x2D, &s_num, 1, srsp, &srlen) == 0 && srlen >= 3) {
            uint8_t trsp[32] = {0}; size_t trlen = sizeof(trsp);
            int has_thresh = (ipmi_cmd(ctx, 0x04, 0x27, &s_num, 1, trsp, &trlen) == 0);

            if (!(srsp[2] & 0x40)) {}
                //printf("%-16s | %-8s | %-10s | na  ", name, "na", unit_str);
            else {
                val = calc_val(srsp[1], m, b, k1, k2);
                if (!strcmp(unit_str, "Watts") && val > 2000.0) {
                    val /= 1000.0;
                }
                uint64_t state = (srsp[2] & 0x20) ? 0 : (srsp[2] & 0x08) ? 2 : 1;
            	//printf("ipmi_sensor_status {name=\"%s\", measure=\"%s\"} %lu\n", name, unit_str, state);
                metric_add_labels2("ipmi_sensor_status", &state, DATATYPE_UINT, ac->system_carg, "name", name, "measure", unit_str);
            }

            if (has_thresh) {
                if ((sdr[18] & 0x01) && trlen > 2) ipmi_set_metric("ipmi_sensor_lower_non_critical", trsp, 2, m, b, k1, k2, name, unit_str);

                if ((sdr[18] & 0x02) && trlen > 3) ipmi_set_metric("ipmi_sensor_lower_critical", trsp, 3, m, b, k1, k2, name, unit_str);

                if ((sdr[18] & 0x04) && trlen > 4) ipmi_set_metric("ipmi_sensor_lower_non_recoverable", trsp, 4, m, b, k1, k2, name, unit_str);

                if ((sdr[19] & 0x08) && trlen > 5) ipmi_set_metric("ipmi_sensor_upper_non_critical", trsp, 5, m, b, k1, k2, name, unit_str);

                if ((sdr[19] & 0x10) && trlen > 6) ipmi_set_metric("ipmi_sensor_upper_critical", trsp, 6, m, b, k1, k2, name, unit_str);

                if ((sdr[19] & 0x20) && trlen > 1) ipmi_set_metric("ipmi_sensor_upper_non_recoverable", trsp, 1, m, b, k1, k2, name, unit_str);

            }
            if (srsp[2] & 0x40) {
            	//printf("ipmi_sensor_stat {name=\"%s\", measure=\"%s\"} %lf\n", name, unit_str, val);
                metric_add_labels2("ipmi_sensor_stat", &val, DATATYPE_DOUBLE, ac->system_carg, "name", name, "measure", unit_str);
            }
        }
next_record:
        if (next == 0) break;
    }
}

int cache_sdr(struct ipmi_ctx *ctx, sensor_map *smap) {
    uint16_t next = 0;
    uint8_t res_d[10] = {0};
    size_t res_l;
    int smap_cnt = 0;
    while (next != 0xFFFF && smap_cnt < 1024) {
        res_l = sizeof(res_d);
        if (ipmi_cmd(ctx, IPMI_NETFN_STORAGE, 0x22, NULL, 0, res_d, &res_l) != 0) break;
        if (res_l < 3) break;
        uint8_t req[] = { res_d[1], res_d[2], next & 0xFF, next >> 8, 0, 0xFF };
        uint8_t rsp[256] = {0};
        size_t rl = sizeof(rsp);
        if (ipmi_cmd(ctx, IPMI_NETFN_STORAGE, 0x23, req, 6, rsp, &rl) != 0) break;
        if (rl < 6) break; /* need next-id + minimal SDR header */
        next = rsp[1] | (rsp[2] << 8);
        uint8_t *sdr = &rsp[3];
        size_t sdr_len = rl - 3;
        if (sdr_len < 9)
            break;
        uint8_t type = sdr[3];
        uint8_t s_num = 0;
        int id_idx = -1;
        if (type == 0x01) { s_num = sdr[7]; id_idx = 47; }
        else if (type == 0x02) { s_num = sdr[7]; id_idx = 31; }
        else if (type == 0x03) { s_num = sdr[8]; id_idx = 16; }

        if (id_idx != -1) {
            smap[smap_cnt].num = s_num;
            if ((size_t)id_idx >= sdr_len) {
                if (next == 0) break;
                continue;
            }
            int len = sdr[id_idx] & 0x1F;
            if (len < 0) len = 0;
            if ((size_t)(id_idx + 1) > sdr_len) len = 0;
            else if ((size_t)(id_idx + 1 + len) > sdr_len) len = (int)(sdr_len - (size_t)(id_idx + 1));
            if (len > 31) len = 31;
            memset(smap[smap_cnt].name, 0, 32);
            memcpy(smap[smap_cnt].name, &sdr[id_idx + 1], len);
            ++smap_cnt;
        }
        if (next == 0) break;
    }

    return smap_cnt;
}

const char* get_sname(uint8_t type, uint8_t num, char *buf, sensor_map *smap, int smap_cnt) {
    if (num == 0x00) {
        if (type == 0x1f) return "OS Boot";
        if (type == 0x20) return "OS Critical Stop";
        if (type == 0x7) return "Processor";
        return "System Event";
    }
    for (int i = 0; i < smap_cnt; i++) {
        if (smap[i].num == num) {
            if (strncmp(smap[i].name, "Chassis Intru", 13) == 0) return "Chassis Intrusion";
            return smap[i].name;
        }
    }
    sprintf(buf, "Unknown #0x%02x", num);
    return buf;
}

void ipmi_eventlog_key_for(void *funcarg, void* arg)
{
    eventlog_node *eventnode = arg;
    context_arg *carg = funcarg;

    metric_add_labels3("ipmi_eventlog_key", &eventnode->val, DATATYPE_UINT, carg, "key",  eventnode->key, "state", eventnode->state, "resource", eventnode->resource);
    free(eventnode);
    carg->parser_status = 1;
}

void do_sel_elist(struct ipmi_ctx *ctx) {
    uint16_t next = 0;
    uint8_t rsp[64] = {0};
    size_t rl;
    char n_buf[32];
    sensor_map smap[1024] = {{0}};
    int smap_cnt = cache_sdr(ctx, smap);
    uint64_t num = 0;
    alligator_ht *event_log = alligator_ht_init(NULL);
    while (next != 0xFFFF) {
        uint8_t req[] = { 0, 0, next & 0xFF, next >> 8, 0, 0xFF };
        rl = sizeof(rsp);
        memset(rsp, 0, sizeof(rsp));
        if (ipmi_cmd(ctx, IPMI_NETFN_STORAGE, 0x43, req, 6, rsp, &rl) != 0) break;
        if (rl < 6) break;
        next = rsp[1] | (rsp[2] << 8);
        if (rsp[5] == 0x02 && rl >= 17) {
            uint32_t ts = rsp[6]|(rsp[7]<<8)|(rsp[8]<<16)|(rsp[9]<<24);
            time_t t = (time_t)ts; struct tm tm; gmtime_r(&t, &tm);
            char ts_s[32]; strftime(ts_s, 32, "%m/%d/%Y | %H:%M:%S", &tm);
            uint8_t s_type = rsp[13], s_num = rsp[14], ev_dir = rsp[15], d1 = rsp[16];
            const char *name = get_sname(s_type, s_num, n_buf, smap, smap_cnt);
            char desc[128] = "";
            const char *asserted = (ev_dir & 0x80) ? "Deasserted" : "Asserted";

            uint8_t event_type = ev_dir & 0x7F;
            if (event_type == 0x01) { // Threshold Event
                int off = d1 & 0x0F;
                const char *trig = "Threshold";
                if (off == 0x00) trig = "Lower Non-critical low";
                else if (off == 0x02) trig = "Lower Critical low";
                else if (off == 0x04) trig = "Lower Non-recoverable low";
                else if (off == 0x07) trig = "Upper Non-critical high";
                else if (off == 0x08) trig = "Upper Critical high";
                else if (off == 0x09) trig = "Upper Non-recoverable high";

                sprintf(desc, "%s", trig);
            }
            else if (s_type == 0x07) { // Processor
                if ((d1 & 0x0F) == 0x00) strcpy(desc, "IERR (Internal Error)");
                else if ((d1 & 0x0F) == 0x01) strcpy(desc, "Thermal Trip");
            }
            else if (s_type == 0x1f || s_type == 0x20) { // OS Events
                if (d1 == 0x01) strcpy(desc, "C: boot completed");
                else if (d1 == 0x03) strcpy(desc, "OS graceful shutdown");
                else if (d1 == 0x07) strcpy(desc, "Installation started");
                else if (d1 == 0x08) strcpy(desc, "Installation completed");
                else {
			        sprintf(desc, "OS Event");
			        //printf("DEBUG: OS Event [0x%02x]", d1);
		        }
            } else {
                sprintf(desc, "Sensor-specific");
		        //printf("DEBUG: Sensor-specific [0x%02x]", d1);
            }

            //printf(" %3x | %s | %-16s | %s | %s\n", rsp[3]|(rsp[4]<<8), ts_s, name, desc, asserted);
            //printf("ipmi_eventlog_key {key=\"%s\", state=\"%s\", resource=\"%s\"} 1\n", name, asserted, desc);
            ++num;
            eventlog_node *eventnode = alligator_ht_search(event_log, eventlog_node_compare, name, tommy_strhash_u32(0, name));
            if (!eventnode) {
                eventnode = calloc(1, sizeof(*eventnode));
                strlcpy(eventnode->key, name, 255);
                strlcpy(eventnode->state, asserted, 255);
                strlcpy(eventnode->resource, desc, 255);
                eventnode->val = 1;
                alligator_ht_insert(event_log, &(eventnode->node), eventnode, tommy_strhash_u32(0, eventnode->key));
            }
            else {
                ++(eventnode->val);
            }
        }
        if (next == 0) break;
    }
    alligator_ht_foreach_arg(event_log, ipmi_eventlog_key_for, ac->system_carg);
    alligator_ht_done(event_log);
    free(event_log);
    metric_add_auto("ipmi_eventlog_size", &num, DATATYPE_UINT, ac->system_carg);
}

static int ipmi_get_status_sync(void) {
    putenv("ipmi_get_status");
    struct ipmi_ctx ctx;
    ctx.fd = open("/dev/ipmi0", O_RDONLY | O_NONBLOCK);
    if (ctx.fd < 0) {
        perror("open /dev/ipmi0");
        return 1;
    }

    do_chassis_status(&ctx);
    get_dcmi_power_metrics(&ctx);
    do_prom_lan_print(&ctx);
    do_sensor_list(&ctx);
    do_sel_elist(&ctx);

    close(ctx.fd);
    return 0;
}

static uv_work_t ipmi_work;
static volatile int ipmi_work_pending;

static void ipmi_work_cb(uv_work_t *req) {
    (void)req;
    ipmi_get_status_sync();
}

static void ipmi_after_work_cb(uv_work_t *req, int status) {
    (void)req;
    (void)status;
    ipmi_work_pending = 0;
}

void ipmi_schedule_get_status(void) {
    uv_loop_t *loop = uv_default_loop();
    if (!loop)
        return;
    if (ipmi_work_pending)
        return;
    ipmi_work_pending = 1;
    uv_queue_work(loop, &ipmi_work, ipmi_work_cb, ipmi_after_work_cb);
}