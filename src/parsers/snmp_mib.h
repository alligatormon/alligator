#pragma once
#include <stddef.h>

/*
 * Load *.txt / *.mib from colon-separated dirs (env snmp_mib_dirs on the scrape context).
 * Parses OBJECT IDENTIFIER ::= { ... } (for resolution) and MODULE-IDENTITY ... ::= { ... }.
 * The reverse OID→name map uses MODULE-IDENTITY only (avoids `ip` vs `ipMIB`, compliance OIDs), plus a few legacy
 * sysORID roots agents still report (e.g. 1.3.6.1.2.1.4 → ipMIB while IP-MIB registers ipMIB at mib-2 48).
 * Cached until snmp_mib_dirs changes. Safe to call repeatedly.
 * Returns 1 if MIBs were (re)loaded, 0 if dirs unchanged (cache hit).
 */
int snmp_mib_ensure_loaded(const char *colon_dirs);

/* Longest-prefix match: dotted OID -> symbol (e.g. ipMIB). */
void snmp_mib_lookup_symbol(const char *dotted_oid, char *out, size_t outcap);

/* Count of MODULE-IDENTITY OIDs in the reverse map (after last load). */
size_t snmp_mib_oid_map_size(void);
