#ifndef LIWUS_CA_TRUST_ANCHORS_H
#define LIWUS_CA_TRUST_ANCHORS_H

#include <stddef.h>
#include "bearssl.h"

extern const br_x509_trust_anchor liwus_trust_anchors[];
extern const size_t liwus_trust_anchors_count;

#endif
