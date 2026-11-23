// A small XML reader for podcast feeds (RSS): tags, their attributes, text and
// CDATA, one token at a time over a text in memory. It skips comments,
// declarations and processing instructions, and doesn't check the document
// is well formed: a broken feed gives what could be read. Pure logic.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define XML_NAME_MAX 64

typedef enum {
    XML_END,    // Nothing more.
    XML_OPEN,   // <name ...>: name, and its attributes with xmlAttr.
    XML_CLOSE,  // </name>, or the end of <name ... />: name.
    XML_TEXT,   // Text or CDATA between tags: xmlText. May come in pieces.
} XmlToken;

typedef struct {
    const char *at, *end;
    char name[XML_NAME_MAX];  // With its prefix ("itunes:image"), as written.
    const char *attrs;        // The open tag's attributes, raw, while it's the token.
    size_t attrs_length;
    const char *text;         // The text token, raw.
    size_t text_length;
    bool text_raw;            // CDATA: no entities to decode.
    bool close_next;          // <name ... /> gives XML_CLOSE next.
} XmlReader;

void xmlBegin(XmlReader *r, const char *text, size_t length);
XmlToken xmlNext(XmlReader *r);

// The value of the current open tag's attribute, entities decoded. False (and
// out empty) if it has none by that name.
bool xmlAttr(const XmlReader *r, const char *name, char *out, size_t size);
// Appends the current text token, entities decoded, to out (NUL-terminated,
// cut at size). Returns the new length.
size_t xmlTextAppend(const XmlReader *r, char *out, size_t size, size_t length);
