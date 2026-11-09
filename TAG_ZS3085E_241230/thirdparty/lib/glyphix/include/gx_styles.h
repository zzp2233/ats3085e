/*
 * This file is part of Glyphix.
 * Copyright (c) 2023-2024, Glyphix Development Team.
 */
#pragma once

#include "gx_sharedref.h"
#include "gx_string.h"
#include "gx_vector.h"

namespace gx {
class Style;
class StyleSet;

class Styles {
public:
    enum StateType { Normal = 0, Active, Disabled, Focus, Selected, UnknownState };
    enum PartType { Content = 0, Range, Bar, UnknownPart };

    Styles();
    ~Styles();

    GX_NODISCARD bool isEmpty() const;
    GX_NODISCARD const Style &style(PartType part = Content, StateType state = Normal) const;
    GX_NODISCARD const StyleSet &styleSet() const;
    GX_NODISCARD static PartType part(const String &part);
    GX_NODISCARD static StateType state(const String &state);
    GX_NODISCARD static String partToString(PartType part);
    GX_NODISCARD static String stateToString(StateType state);
    GX_NODISCARD static std::pair<PartType, StateType>
    partState(const String &key = "content.normal");
    void setStyle(const Style &style, PartType part = Content, StateType state = Normal);

private:
    class Private;
    SharedRef<Private> m_d;
};

class StyleSet {
    typedef Vector<std::pair<int, Style> > Buckets;

public:
    typedef Buckets::iterator iterator;             // NOLINT(readability-*)
    typedef Buckets::const_iterator const_iterator; // NOLINT(readability-*)

    StyleSet();
    ~StyleSet();

    GX_NODISCARD bool isEmpty() const;
    GX_NODISCARD Style *style(int key);
    GX_NODISCARD const Style *style(int key) const;
    GX_NODISCARD Style &styleSlot(int key);
    Style &setStyle(int key, const Style &style);
    bool deleteStyle(int key);

    GX_NODISCARD iterator begin() { return m_styles.begin(); }
    GX_NODISCARD iterator end() { return m_styles.end(); }
    GX_NODISCARD const_iterator begin() const { return m_styles.begin(); }
    GX_NODISCARD const_iterator end() const { return m_styles.end(); }

    GX_NODISCARD static int encodeKey(Styles::PartType part, Styles::StateType state);
    GX_NODISCARD static Styles::PartType decodePart(int key);
    GX_NODISCARD static Styles::StateType decodeState(int key);

private:
    GX_NODISCARD Buckets::const_iterator lowerBound(int key) const;
    Buckets m_styles;
};

inline int StyleSet::encodeKey(Styles::PartType part, Styles::StateType state) {
    return (int(part) << 8) | int(state);
}

inline Styles::PartType StyleSet::decodePart(int key) { return Styles::PartType(key >> 8); }

inline Styles::StateType StyleSet::decodeState(int key) { return Styles::StateType(key & 0xff); }

} // namespace gx
