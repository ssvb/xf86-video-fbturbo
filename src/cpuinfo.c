/*
 * Copyright © 2013 Siarhei Siamashka <siarhei.siamashka@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpuinfo.h"

#if defined(__linux__) || defined(ANDROID) || defined(__ANDROID__)

#define MAXBUFSIZE 16384

static const char *cpuinfo_match_prefix(const char *s, const char *prefix)
{
    const char *result;
    if (strncmp(s, prefix, strlen(prefix)) != 0)
        return NULL;
    result = strchr(s, ':');
    if (!result)
        return NULL;
    result++;
    while (*result && (*result == ' ' || *result == '\t'))
        result++;
    return result;
}

static int is_space_or_eol(char x)
{
    return (x == 0 || x == ' ' || x == '\t' || x == '\n');
}

static int find_feature(const char *buffer, const char *feature)
{
    const char *s = buffer;
    while (*s) {
        char *t = strstr(s, feature);
        if (!t)
            return 0;
        if (!is_space_or_eol(*(t + strlen(feature)))) {
            s++;
            continue;
        }
        if (t == buffer)
            return 1;
        if (t > buffer && is_space_or_eol(*(t - 1)))
            return 1;
        s++;
    }
    return 0;
}

static int parse_proc_cpuinfo(cpuinfo_t *cpuinfo)
{
    char *buffer = (char *)malloc(MAXBUFSIZE);
    FILE *fd;
    const char *val;

    if (!buffer)
        return 0;

    fd = fopen("/proc/cpuinfo", "r");
    if (!fd) {
        free(buffer);
        return 0;
    }

    while (fgets(buffer, MAXBUFSIZE, fd)) {
        if (!strchr(buffer, '\n') && !feof(fd)) {
            fclose(fd);
            free(buffer);
            return 0;
        }
        if ((val = cpuinfo_match_prefix(buffer, "Features"))) {
            cpuinfo->has_arm_edsp = find_feature(val, "edsp");
            cpuinfo->has_arm_vfp  = find_feature(val, "vfp");
            cpuinfo->has_arm_neon = find_feature(val, "neon");
            cpuinfo->has_arm_wmmx = find_feature(val, "iwmmxt");
        }
        else if ((val = cpuinfo_match_prefix(buffer, "CPU implementer"))) {
            if (sscanf(val, "%i", &cpuinfo->arm_implementer) != 1) {
                fclose(fd);
                free(buffer);
                return 0;
            }
        }
        else if ((val = cpuinfo_match_prefix(buffer, "CPU architecture"))) {
            if (sscanf(val, "%i", &cpuinfo->arm_architecture) != 1) {
                fclose(fd);
                free(buffer);
                return 0;
            }
        }
        else if ((val = cpuinfo_match_prefix(buffer, "CPU variant"))) {
            if (sscanf(val, "%i", &cpuinfo->arm_variant) != 1) {
                fclose(fd);
                free(buffer);
                return 0;
            }
        }
        else if ((val = cpuinfo_match_prefix(buffer, "CPU part"))) {
            if (sscanf(val, "%i", &cpuinfo->arm_part) != 1) {
                fclose(fd);
                free(buffer);
                return 0;
            }
        }
        else if ((val = cpuinfo_match_prefix(buffer, "CPU revision"))) {
            if (sscanf(val, "%x", &cpuinfo->arm_revision) != 1) {
                fclose(fd);
                free(buffer);
                return 0;
            }
        }
    }
    fclose(fd);
    free(buffer);
    return 1;
}

#else

static int parse_proc_cpuinfo(cpuinfo_t *cpuinfo)
{
    return 0;
}

#endif

cpuinfo_t *cpuinfo_init()
{
    cpuinfo_t *cpuinfo = calloc(sizeof(cpuinfo_t), 1);
    if (!cpuinfo)
        return NULL;

    if (!parse_proc_cpuinfo(cpuinfo)) {
        cpuinfo->processor_name = strdup("Unknown");
        return cpuinfo;
    }

    if (cpuinfo->arm_implementer == 0x41 && cpuinfo->arm_part == 0xC0F) {
        cpuinfo->processor_name = strdup("ARM Cortex-A15");
    } else if (cpuinfo->arm_implementer == 0x41 && cpuinfo->arm_part == 0xC09) {
        if (cpuinfo->has_arm_neon)
            cpuinfo->processor_name = strdup("ARM Cortex-A9");
        else
            cpuinfo->processor_name = strdup("ARM Cortex-A9 without NEON (Tegra2?)");
    } else if (cpuinfo->arm_implementer == 0x41 && cpuinfo->arm_part == 0xC08) {
        if (cpuinfo->arm_variant > 1)
            cpuinfo->processor_name = strdup("Late ARM Cortex-A8 (NEON can bypass L1 cache)");
        else
            cpuinfo->processor_name = strdup("Early ARM Cortex-A8");
    } else if (cpuinfo->arm_implementer == 0x41 && cpuinfo->arm_part == 0xC07) {
        cpuinfo->processor_name = strdup("ARM Cortex-A7");
    } else if (cpuinfo->arm_implementer == 0x41 && cpuinfo->arm_part == 0xC05) {
        cpuinfo->processor_name = strdup("ARM Cortex-A5");
    } else if (cpuinfo->arm_implementer == 0x41 && cpuinfo->arm_part == 0xB76) {
        cpuinfo->processor_name = strdup("ARM1176");
    } else if (cpuinfo->arm_implementer == 0x56 && cpuinfo->arm_part == 0x581) {
        cpuinfo->processor_name = strdup("Marvell PJ4");
    } else {
        cpuinfo->processor_name = strdup("Unknown");
    }

    return cpuinfo;
}

void cpuinfo_close(cpuinfo_t *cpuinfo)
{
    free(cpuinfo->processor_name);
    free(cpuinfo);
}
