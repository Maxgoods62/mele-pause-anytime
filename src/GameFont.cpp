#include "MelePause/GameFont.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

// The source is a GFX movie (Scaleform's SWF, version 8) with one DefineFont3 tag per font:
//   FontID u16, flags u8, language u8, name length u8, name, glyph count N u16,
//   offset table u32[N], code table offset u32, N SHAPEs, code table u16[N],
//   ascent, descent, leading u16, advances u16[N], N RECTs, kerning count u16, kerning (u16, u16, s16)[count].
// All offsets count from the start of the offset table.
//
// Three things differ from the SWF spec or from what one expects of it, all measured on the game's 13 language files:
//  - Advances are unsigned. Biomass's W, Æ, Œ, Ŵ, Ж and Щ go past 32767 and are right only when read as u16.
//  - The bounds table holds zeros only, so the boxes are computed from the outlines.
//  - A glyph's first contour can start at the origin with no MoveTo (л in Biomass, Æ and Ǽ in Myriad).
//
// Coordinates are 20480 per EM, y down, all multiples of 10 in the game's fonts. So 2048 units per EM keeps them exactly, and
// 20480 would not fit TrueType's int16 (Biomass's Œ reaches x 45020). Curves are quadratic in both formats.
//
// Fill: every glyph uses FillStyle0 alone, and outer contours run opposite to holes, on all 127,394 contours of the three
// fonts. Flipping y gives TrueType's own convention, so no contour is reversed. A glyph that uses styles in any other way is
// refused, since nothing says how it would fill.
namespace MelePause::GameFont
{
    namespace
    {
        using Bytes = std::span<unsigned char const>;

        /// The game's movie is 7,042,785 bytes.
        constexpr std::size_t MaxMovieBytes = 64u << 20;
        /// The game's movie has 17 tags.
        constexpr int MaxTags = 4096;
        /// The whole of the largest game font (Myriad) is 2.5 MB.
        constexpr std::size_t MaxOutputBytes = 16u << 20;
        /// The game's largest glyphs have 271 points and 22 contours. These caps also bound what stb_truetype spends on a glyph.
        constexpr std::size_t MaxGlyphPoints = 4096;
        constexpr std::size_t MaxGlyphContours = 256;
        /// TrueType's numGlyphs is a u16, and .notdef takes one.
        constexpr std::size_t MaxGlyphs = 65534;
        /// The cmap format 4 subtable's length is a u16: 16 bytes, plus 8 per segment.
        constexpr std::size_t MaxCmapSegments = 8189;

        constexpr std::int32_t SwfUnitsPerEm = 20480;
        constexpr std::int32_t UnitsPerEm = 2048;
        constexpr std::int32_t SwfUnitsPerUnit = SwfUnitsPerEm / UnitsPerEm;
        /// A coordinate past 4 EM is refused (the game's fonts stay within 2.2 EM). A glyph then is at most 8 EM wide, which keeps
        /// every TrueType delta in int16 and a bitmap at a sane size.
        constexpr std::int32_t MaxCoordinate = 4 * SwfUnitsPerEm;
        /// Ascent plus descent, the divisor of ImGui's scale, is refused outside these (the game's fonts have 1.13 to 1.20 EM):
        /// a very small one would blow every bitmap up.
        constexpr std::int32_t MinLineHeight = SwfUnitsPerEm / 2;
        constexpr std::int32_t MaxLineHeight = 4 * SwfUnitsPerEm;

        constexpr unsigned TagEnd = 0;
        constexpr unsigned TagDefineFont3 = 75;

        constexpr unsigned FontHasLayout = 0x80;
        constexpr unsigned FontWideOffsets = 0x08;
        constexpr unsigned FontWideCodes = 0x04;

        constexpr unsigned EdgeNewStyles = 0x10;
        constexpr unsigned EdgeLineStyle = 0x08;
        constexpr unsigned EdgeFillStyle1 = 0x04;
        constexpr unsigned EdgeFillStyle0 = 0x02;
        constexpr unsigned EdgeMoveTo = 0x01;

        constexpr unsigned char PointOnCurve = 0x01;
        constexpr unsigned char PointShortX = 0x02;
        constexpr unsigned char PointShortY = 0x04;
        constexpr unsigned char PointSameOrPositiveX = 0x10;
        constexpr unsigned char PointSameOrPositiveY = 0x20;

        /// Little-endian reads of untrusted bytes. A read past the end gives zeros and sets Failed, which stays set.
        class ByteReader
        {
        public:
            explicit ByteReader(Bytes bytes) : bytes_(bytes) {}

            bool Failed() const { return failed_; }
            std::size_t Position() const { return position_; }
            bool AtEnd() const { return position_ == bytes_.size(); }

            Bytes Take(std::size_t count)
            {
                if (failed_ || count > bytes_.size() - position_)
                {
                    failed_ = true;
                    return {};
                }
                auto const taken = bytes_.subspan(position_, count);
                position_ += count;
                return taken;
            }

            unsigned U8()
            {
                auto const bytes = Take(1);
                return bytes.empty() ? 0u : bytes[0];
            }

            unsigned U16()
            {
                auto const bytes = Take(2);
                return bytes.empty() ? 0u : bytes[0] | static_cast<unsigned>(bytes[1]) << 8;
            }

            std::uint32_t U32()
            {
                auto const low = U16();
                return low | static_cast<std::uint32_t>(U16()) << 16;
            }

        private:
            Bytes bytes_;
            std::size_t position_ = 0;
            bool failed_ = false;
        };

        /// Reads bits, highest first, as SWF packs them. A read past the end gives zeros and sets Failed, which stays set.
        class BitReader
        {
        public:
            explicit BitReader(Bytes bytes) : bytes_(bytes), size_(bytes.size() * 8) {}

            bool Failed() const { return failed_; }

            /// At most 31 bits.
            std::uint32_t Unsigned(unsigned count)
            {
                if (failed_ || count > size_ - position_)
                {
                    failed_ = true;
                    return 0;
                }
                std::uint32_t value = 0;
                while (count > 0)
                {
                    auto const used = static_cast<unsigned>(position_ & 7);
                    auto const take = std::min(8 - used, count);
                    auto const bits = (static_cast<unsigned>(bytes_[position_ >> 3]) >> (8 - used - take)) & ((1u << take) - 1);
                    value = value << take | bits;
                    position_ += take;
                    count -= take;
                }
                return value;
            }

            /// At most 31 bits, two's complement.
            std::int32_t Signed(unsigned count)
            {
                if (count == 0) return 0;
                auto const sign = std::uint32_t{ 1 } << (count - 1);
                return static_cast<std::int32_t>(Unsigned(count) ^ sign) - static_cast<std::int32_t>(sign);
            }

        private:
            Bytes bytes_;
            std::size_t size_;
            std::size_t position_ = 0;
            bool failed_ = false;
        };

        unsigned U16At(Bytes bytes, std::size_t index)
        {
            return bytes[index * 2] | static_cast<unsigned>(bytes[index * 2 + 1]) << 8;
        }

        std::uint32_t U32At(Bytes bytes, std::size_t index)
        {
            auto const at = index * 4;
            return bytes[at] | static_cast<std::uint32_t>(bytes[at + 1]) << 8 | static_cast<std::uint32_t>(bytes[at + 2]) << 16
                   | static_cast<std::uint32_t>(bytes[at + 3]) << 24;
        }

        /// Nearest, halves away from zero. Exact on the game's fonts, whose coordinates are all multiples of 10.
        std::int32_t ToUnits(std::int32_t swf)
        {
            auto const half = SwfUnitsPerUnit / 2;
            return swf >= 0 ? (swf + half) / SwfUnitsPerUnit : -((half - swf) / SwfUnitsPerUnit);
        }

        /// The tables of a font inside the movie, all checked to be inside its tag.
        struct FontTables
        {
            std::size_t Count = 0;
            /// From the offset table on. Holds Count + 1 ascending u32 offsets, the last one the code table's.
            Bytes Body;
            Bytes Codes;
            Bytes Advances;
            /// What follows the advances: the bounds, then the kerning.
            Bytes Trailer;
            std::int32_t Ascent = 0;
            std::int32_t Descent = 0;
            std::int32_t Leading = 0;

            Bytes Shape(std::size_t index) const
            {
                auto const first = U32At(Body, index);
                return Body.subspan(first, U32At(Body, index + 1) - first);
            }
        };

        /// The part of a DefineFont3 tag up to the glyph count, in which the name is.
        struct FontHeader
        {
            unsigned Flags = 0;
            Bytes Name;
            std::size_t Count = 0;
            std::size_t Size = 0;
        };

        bool ReadHeader(Bytes tag, FontHeader& header)
        {
            ByteReader reader(tag);
            reader.U16(); // FontID
            header.Flags = reader.U8();
            reader.U8(); // language
            header.Name = reader.Take(reader.U8());
            header.Count = reader.U16();
            header.Size = reader.Position();
            return !reader.Failed();
        }

        bool SameName(Bytes name, std::string_view wanted)
        {
            // Some exporters store a terminating zero.
            if (!name.empty() && name.back() == 0) name = name.first(name.size() - 1);
            return std::equal(name.begin(), name.end(), wanted.begin(), wanted.end(),
                              [](unsigned char a, char b) { return a == static_cast<unsigned char>(b); });
        }

        bool ReadTables(Bytes tag, FontHeader const& header, FontTables& font)
        {
            auto const required = FontHasLayout | FontWideOffsets | FontWideCodes;
            if ((header.Flags & required) != required || header.Count == 0) return false;

            font.Count = header.Count;
            font.Body = tag.subspan(header.Size);
            auto const offsetBytes = (font.Count + 1) * 4;
            if (offsetBytes > font.Body.size()) return false;

            // Ascending offsets also keep the shapes apart, so that a hostile table cannot read one stretch many times over.
            std::uint64_t previous = offsetBytes;
            for (std::size_t i = 0; i <= font.Count; ++i)
            {
                auto const offset = U32At(font.Body, i);
                if (offset < previous) return false;
                previous = offset;
            }
            if (previous > font.Body.size()) return false;

            ByteReader tables(font.Body.subspan(static_cast<std::size_t>(previous)));
            font.Codes = tables.Take(font.Count * 2);
            font.Ascent = static_cast<std::int32_t>(tables.U16());
            font.Descent = static_cast<std::int32_t>(tables.U16());
            font.Leading = static_cast<std::int32_t>(tables.U16());
            font.Advances = tables.Take(font.Count * 2);
            if (tables.Failed()) return false;
            font.Trailer = font.Body.subspan(static_cast<std::size_t>(previous) + tables.Position());

            auto const lineHeight = font.Ascent + font.Descent;
            return lineHeight >= MinLineHeight && lineHeight <= MaxLineHeight;
        }

        /// Walks the movie's tags to the DefineFont3 of that name.
        bool FindFont(Bytes movie, std::string_view name, FontTables& font)
        {
            if (movie.size() > MaxMovieBytes) return false;

            ByteReader reader(movie);
            auto const signature = reader.Take(3);
            auto const version = reader.U8();
            reader.U32(); // file length: the span's size is what counts
            if (reader.Failed() || signature[0] != 'G' || signature[1] != 'F' || signature[2] != 'X' || version != 8) return false;

            // The stage RECT: 5 bits give the size of its 4 fields, then it pads to a byte.
            auto const rectBits = reader.U8() >> 3;
            reader.Take((5 + 4 * rectBits + 7) / 8 - 1);
            reader.U16(); // frame rate
            reader.U16(); // frame count

            for (int i = 0; i < MaxTags && !reader.Failed() && !reader.AtEnd(); ++i)
            {
                auto const header = reader.U16();
                auto const code = header >> 6;
                std::size_t length = header & 0x3F;
                if (length == 0x3F) length = reader.U32();
                auto const tag = reader.Take(length);
                if (reader.Failed() || code == TagEnd) return false;
                if (code != TagDefineFont3) continue;

                FontHeader fontHeader;
                if (!ReadHeader(tag, fontHeader)) return false;
                if (SameName(fontHeader.Name, name)) return ReadTables(tag, fontHeader, font);
            }
            return false;
        }

        std::vector<bool> ToSet(std::span<char32_t const> characters)
        {
            // U+FFFF is out: cmap format 4 ends on that code, and it is not a character.
            std::vector<bool> set(0xFFFF);
            for (auto const character : characters)
                if (character < set.size()) set[character] = true;
            return set;
        }

        struct Choice
        {
            std::uint16_t Code = 0;
            std::uint16_t Index = 0;
        };

        /// The font's glyphs of those characters, by code. The first of a code in the font wins.
        std::vector<Choice> Choose(FontTables const& font, std::span<char32_t const> characters)
        {
            auto wanted = ToSet(characters);
            std::vector<Choice> chosen;
            for (std::size_t i = 0; i < font.Count; ++i)
            {
                auto const code = U16At(font.Codes, i);
                if (code >= wanted.size() || !wanted[code]) continue;
                wanted[code] = false;
                chosen.push_back({ static_cast<std::uint16_t>(code), static_cast<std::uint16_t>(i) });
            }
            std::sort(chosen.begin(), chosen.end(), [](Choice a, Choice b) { return a.Code < b.Code; });
            return chosen;
        }

        struct Point
        {
            std::int16_t X = 0;
            std::int16_t Y = 0;
            bool OnCurve = false;
        };

        /// A glyph in TrueType units, y up. Kept between glyphs for its capacity.
        class Outline
        {
        public:
            void Clear()
            {
                points_.clear();
                ends_.clear();
                start_ = 0;
                open_ = false;
            }

            std::span<Point const> Points() const { return points_; }
            std::span<std::uint16_t const> Ends() const { return ends_; }

            /// Starts a contour at the pen, unless one is open: a contour may have no MoveTo, and then starts at the origin.
            bool Begin(std::int32_t x, std::int32_t y)
            {
                if (open_) return true;
                start_ = points_.size();
                open_ = true;
                return Add(x, y, true);
            }

            bool Add(std::int32_t x, std::int32_t y, bool onCurve)
            {
                if (x < -MaxCoordinate || x > MaxCoordinate || y < -MaxCoordinate || y > MaxCoordinate) return false;
                if (points_.size() >= MaxGlyphPoints) return false;
                Point const point{ static_cast<std::int16_t>(ToUnits(x)), static_cast<std::int16_t>(-ToUnits(y)), onCurve };
                if (onCurve && points_.size() > start_ && Same(point, points_.back())) return true;
                points_.push_back(point);
                return true;
            }

            /// Ends the open contour, if any. TrueType closes a contour by itself, so a last point on the first one goes. What
            /// is left with fewer than 3 points has no area and goes as well.
            bool Close()
            {
                if (!open_) return true;
                open_ = false;
                if (points_.size() > start_ + 1 && Same(points_.back(), points_[start_])) points_.pop_back();
                if (points_.size() - start_ < 3)
                {
                    points_.resize(start_);
                    return true;
                }
                if (ends_.size() >= MaxGlyphContours) return false;
                ends_.push_back(static_cast<std::uint16_t>(points_.size() - 1));
                return true;
            }

        private:
            static bool Same(Point a, Point b) { return a.OnCurve && b.OnCurve && a.X == b.X && a.Y == b.Y; }

            std::vector<Point> points_;
            std::vector<std::uint16_t> ends_;
            std::size_t start_ = 0;
            bool open_ = false;
        };

        /// Decodes a SHAPE. Each loop eats at least 6 bits and the outline caps its points, so bad data ends soon.
        bool ReadOutline(Bytes shape, Outline& outline)
        {
            outline.Clear();
            BitReader reader(shape);
            auto const fillBits = reader.Unsigned(4);
            auto const lineBits = reader.Unsigned(4);
            std::int32_t x = 0;
            std::int32_t y = 0;

            while (!reader.Failed())
            {
                if (reader.Unsigned(1) == 0)
                {
                    auto const flags = reader.Unsigned(5);
                    if (flags == 0) return outline.Close();
                    if ((flags & EdgeNewStyles) != 0) return false;
                    if ((flags & EdgeMoveTo) != 0)
                    {
                        if (!outline.Close()) return false;
                        auto const bits = reader.Unsigned(5);
                        x = reader.Signed(bits);
                        y = reader.Signed(bits);
                        // Now, not at the first edge: the sums below must not overflow.
                        if (x < -MaxCoordinate || x > MaxCoordinate || y < -MaxCoordinate || y > MaxCoordinate) return false;
                    }
                    if ((flags & EdgeFillStyle0) != 0 && reader.Unsigned(fillBits) != 1) return false;
                    if ((flags & EdgeFillStyle1) != 0 && reader.Unsigned(fillBits) != 0) return false;
                    if ((flags & EdgeLineStyle) != 0 && reader.Unsigned(lineBits) != 0) return false;
                    continue;
                }

                auto const straight = reader.Unsigned(1) != 0;
                auto const bits = reader.Unsigned(4) + 2;
                if (!outline.Begin(x, y)) return false;
                if (straight)
                {
                    auto const general = reader.Unsigned(1) != 0;
                    auto const vertical = !general && reader.Unsigned(1) != 0;
                    if (general || !vertical) x += reader.Signed(bits);
                    if (general || vertical) y += reader.Signed(bits);
                    if (!outline.Add(x, y, true)) return false;
                }
                else
                {
                    x += reader.Signed(bits);
                    y += reader.Signed(bits);
                    if (!outline.Add(x, y, false)) return false;
                    x += reader.Signed(bits);
                    y += reader.Signed(bits);
                    if (!outline.Add(x, y, true)) return false;
                }
            }
            return false;
        }

        void Put16(std::vector<unsigned char>& out, std::int32_t value)
        {
            out.push_back(static_cast<unsigned char>(value >> 8 & 0xFF));
            out.push_back(static_cast<unsigned char>(value & 0xFF));
        }

        void Put32(std::vector<unsigned char>& out, std::uint32_t value)
        {
            Put16(out, static_cast<std::int32_t>(value >> 16));
            Put16(out, static_cast<std::int32_t>(value & 0xFFFF));
        }

        /// The tables that grow glyph by glyph, with the extremes the fixed tables need.
        struct GlyphTables
        {
            std::vector<unsigned char> Glyf;
            std::vector<unsigned char> Loca;
            std::vector<unsigned char> Hmtx;
            std::int32_t Count = 0;
            std::int32_t MaxPoints = 0;
            std::int32_t MaxContours = 0;
            std::int32_t XMin = 0;
            std::int32_t YMin = 0;
            std::int32_t XMax = 0;
            std::int32_t YMax = 0;
            std::int32_t MaxAdvance = 0;
            std::int32_t MinLeft = 0;
            std::int32_t MinRight = 0;
            std::int32_t MaxExtent = 0;
            bool HasOutline = false;
            /// The point arrays of the glyph being written. Kept for their capacity.
            std::vector<unsigned char> Flags;
            std::vector<unsigned char> XBytes;
            std::vector<unsigned char> YBytes;

            /// A point's delta on one axis: 0 needs no byte, under 256 one byte and a sign flag, else two bytes.
            static unsigned char PutDelta(std::vector<unsigned char>& out, std::int32_t delta, unsigned char shortFlag,
                                          unsigned char sameFlag)
            {
                if (delta == 0) return sameFlag;
                if (delta > -256 && delta < 256)
                {
                    out.push_back(static_cast<unsigned char>(delta < 0 ? -delta : delta));
                    return delta > 0 ? static_cast<unsigned char>(shortFlag | sameFlag) : shortFlag;
                }
                Put16(out, delta);
                return 0;
            }

            /// Adds a glyph, whose outline may be empty. Its index is the number of glyphs before it.
            void Add(Outline const& outline, std::int32_t advance)
            {
                Put32(Loca, static_cast<std::uint32_t>(Glyf.size()));
                ++Count;
                MaxAdvance = std::max(MaxAdvance, advance);

                auto const points = outline.Points();
                if (points.empty())
                {
                    Put16(Hmtx, advance);
                    Put16(Hmtx, 0);
                    return;
                }

                std::int32_t left = points[0].X;
                std::int32_t bottom = points[0].Y;
                std::int32_t right = left;
                std::int32_t top = bottom;
                std::int32_t x = 0;
                std::int32_t y = 0;
                Flags.clear();
                XBytes.clear();
                YBytes.clear();
                for (auto const point : points)
                {
                    left = std::min<std::int32_t>(left, point.X);
                    right = std::max<std::int32_t>(right, point.X);
                    bottom = std::min<std::int32_t>(bottom, point.Y);
                    top = std::max<std::int32_t>(top, point.Y);
                    auto flags = point.OnCurve ? PointOnCurve : static_cast<unsigned char>(0);
                    flags |= PutDelta(XBytes, point.X - x, PointShortX, PointSameOrPositiveX);
                    flags |= PutDelta(YBytes, point.Y - y, PointShortY, PointSameOrPositiveY);
                    Flags.push_back(flags);
                    x = point.X;
                    y = point.Y;
                }

                auto const ends = outline.Ends();
                Put16(Glyf, static_cast<std::int32_t>(ends.size()));
                Put16(Glyf, left);
                Put16(Glyf, bottom);
                Put16(Glyf, right);
                Put16(Glyf, top);
                for (auto const end : ends) Put16(Glyf, end);
                Put16(Glyf, 0); // no instructions
                Glyf.insert(Glyf.end(), Flags.begin(), Flags.end());
                Glyf.insert(Glyf.end(), XBytes.begin(), XBytes.end());
                Glyf.insert(Glyf.end(), YBytes.begin(), YBytes.end());
                Glyf.resize((Glyf.size() + 3) & ~std::size_t{ 3 });

                Put16(Hmtx, advance);
                Put16(Hmtx, left);

                auto const rightBearing = advance - right;
                if (!HasOutline)
                {
                    XMin = left;
                    YMin = bottom;
                    XMax = right;
                    YMax = top;
                    MinLeft = left;
                    MinRight = rightBearing;
                    MaxExtent = right;
                    HasOutline = true;
                }
                XMin = std::min(XMin, left);
                YMin = std::min(YMin, bottom);
                XMax = std::max(XMax, right);
                YMax = std::max(YMax, top);
                MinLeft = std::min(MinLeft, left);
                MinRight = std::min(MinRight, rightBearing);
                MaxExtent = std::max(MaxExtent, right);
                MaxPoints = std::max(MaxPoints, static_cast<std::int32_t>(points.size()));
                MaxContours = std::max(MaxContours, static_cast<std::int32_t>(ends.size()));
            }
        };

        /// A format 4 cmap on Windows' Unicode BMP encoding, in idDelta form: glyphs follow codes, so that a run of codes needs
        /// no glyph array. Empty when the runs do not fit its u16 length.
        std::vector<unsigned char> MakeCmap(std::span<Choice const> chosen)
        {
            struct Segment
            {
                std::int32_t First = 0;
                std::int32_t Last = 0;
                std::int32_t Glyph = 0;
            };
            std::vector<Segment> segments;
            std::int32_t glyph = 1; // after .notdef
            for (auto const choice : chosen)
            {
                std::int32_t const code = choice.Code;
                if (!segments.empty() && segments.back().Last + 1 == code) segments.back().Last = code;
                else segments.push_back({ code, code, glyph });
                ++glyph;
            }
            segments.push_back({ 0xFFFF, 0xFFFF, 0 }); // the end marker the format wants; its delta maps it to glyph 0
            if (segments.size() > MaxCmapSegments) return {};

            std::int32_t const count = static_cast<std::int32_t>(segments.size());
            std::int32_t selector = 0;
            while ((2 << selector) <= count) ++selector;
            std::int32_t const searchRange = 2 << selector;

            std::vector<unsigned char> out;
            out.reserve(static_cast<std::size_t>(28 + 8 * count));
            Put16(out, 0); // table version
            Put16(out, 1); // one subtable
            Put16(out, 3); // Windows
            Put16(out, 1); // Unicode BMP
            Put32(out, 12);
            Put16(out, 4); // format
            Put16(out, 16 + 8 * count);
            Put16(out, 0); // language
            Put16(out, 2 * count);
            Put16(out, searchRange);
            Put16(out, selector);
            Put16(out, 2 * count - searchRange);
            for (auto const& segment : segments) Put16(out, segment.Last);
            Put16(out, 0); // reservedPad
            for (auto const& segment : segments) Put16(out, segment.First);
            for (auto const& segment : segments) Put16(out, (segment.Glyph - segment.First) & 0xFFFF);
            for (std::int32_t i = 0; i < count; ++i) Put16(out, 0); // idRangeOffset
            return out;
        }

        std::vector<unsigned char> MakeHead(GlyphTables const& glyphs)
        {
            std::vector<unsigned char> out;
            out.reserve(54);
            Put32(out, 0x00010000); // version
            Put32(out, 0x00010000); // font revision
            Put32(out, 0);          // checkSumAdjustment, filled in by Assemble
            Put32(out, 0x5F0F3CF5); // magic
            Put16(out, 0x0003);     // baseline at y 0, left side bearing at x 0
            Put16(out, UnitsPerEm);
            Put32(out, 0); // created and modified: 8 bytes each
            Put32(out, 0);
            Put32(out, 0);
            Put32(out, 0);
            Put16(out, glyphs.XMin);
            Put16(out, glyphs.YMin);
            Put16(out, glyphs.XMax);
            Put16(out, glyphs.YMax);
            Put16(out, 0); // macStyle
            Put16(out, 8); // lowestRecPPEM
            Put16(out, 2); // fontDirectionHint
            Put16(out, 1); // indexToLocFormat: long
            Put16(out, 0); // glyphDataFormat
            return out;
        }

        std::vector<unsigned char> MakeHhea(FontTables const& font, GlyphTables const& glyphs)
        {
            std::vector<unsigned char> out;
            out.reserve(36);
            Put32(out, 0x00010000); // version
            Put16(out, ToUnits(font.Ascent));
            Put16(out, -ToUnits(font.Descent));
            Put16(out, ToUnits(font.Leading));
            Put16(out, glyphs.MaxAdvance);
            Put16(out, glyphs.MinLeft);
            Put16(out, glyphs.MinRight);
            Put16(out, glyphs.MaxExtent);
            Put16(out, 1); // caret slope rise
            for (int i = 0; i < 7; ++i) Put16(out, 0); // caret slope run and offset, 4 reserved, metric data format
            Put16(out, glyphs.Count);
            return out;
        }

        std::vector<unsigned char> MakeMaxp(GlyphTables const& glyphs)
        {
            std::vector<unsigned char> out;
            out.reserve(32);
            Put32(out, 0x00010000); // version
            Put16(out, glyphs.Count);
            Put16(out, glyphs.MaxPoints);
            Put16(out, glyphs.MaxContours);
            Put16(out, 0); // maxCompositePoints
            Put16(out, 0); // maxCompositeContours
            Put16(out, 1); // maxZones
            for (int i = 0; i < 8; ++i) Put16(out, 0); // instructions and composites: none
            return out;
        }

        std::uint32_t Checksum(Bytes bytes)
        {
            std::uint32_t sum = 0;
            for (std::size_t i = 0; i < bytes.size(); ++i) sum += static_cast<std::uint32_t>(bytes[i]) << (24 - 8 * (i & 3));
            return sum;
        }

        /// The sfnt wrapper: a directory by tag, then the tables, each padded to 4 bytes. Empty past the output cap.
        std::vector<unsigned char> Assemble(std::span<std::pair<char const*, std::vector<unsigned char>> const> tables)
        {
            std::size_t total = 12 + 16 * tables.size();
            for (auto const& table : tables) total += (table.second.size() + 3) & ~std::size_t{ 3 };
            if (total > MaxOutputBytes) return {};

            std::int32_t const count = static_cast<std::int32_t>(tables.size());
            std::int32_t selector = 0;
            while ((2 << selector) <= count) ++selector;
            std::int32_t const searchRange = 16 << selector;

            std::vector<unsigned char> out;
            out.reserve(total);
            Put32(out, 0x00010000);
            Put16(out, count);
            Put16(out, searchRange);
            Put16(out, selector);
            Put16(out, 16 * count - searchRange);

            auto offset = static_cast<std::uint32_t>(12 + 16 * tables.size());
            std::size_t headOffset = 0;
            for (auto const& [tag, bytes] : tables)
            {
                out.insert(out.end(), tag, tag + 4);
                Put32(out, Checksum(bytes));
                Put32(out, offset);
                Put32(out, static_cast<std::uint32_t>(bytes.size()));
                if (std::string_view(tag, 4) == "head") headOffset = offset;
                offset += static_cast<std::uint32_t>((bytes.size() + 3) & ~std::size_t{ 3 });
            }
            for (auto const& table : tables)
            {
                out.insert(out.end(), table.second.begin(), table.second.end());
                out.resize((out.size() + 3) & ~std::size_t{ 3 });
            }

            // head.checkSumAdjustment makes the whole file sum to this constant.
            auto const adjustment = 0xB1B0AFBAu - Checksum(out);
            for (std::size_t i = 0; i < 4; ++i) out[headOffset + 8 + i] = static_cast<unsigned char>(adjustment >> (24 - 8 * i) & 0xFF);
            return out;
        }
    }

    std::vector<unsigned char> Build(std::span<unsigned char const> movie, std::string_view fontName,
                                     std::span<char32_t const> characters)
    {
        FontTables font;
        if (!FindFont(movie, fontName, font)) return {};
        auto const chosen = Choose(font, characters);
        if (chosen.empty() || chosen.size() > MaxGlyphs) return {};

        GlyphTables glyphs;
        Outline outline;
        glyphs.Add(outline, UnitsPerEm / 2); // .notdef: nothing draws it, since ImGui treats glyph 0 as missing
        for (auto const choice : chosen)
        {
            if (!ReadOutline(font.Shape(choice.Index), outline)) return {};
            // Unsigned on purpose: see the top of this file.
            glyphs.Add(outline, ToUnits(static_cast<std::int32_t>(U16At(font.Advances, choice.Index))));
            if (glyphs.Glyf.size() > MaxOutputBytes) return {};
        }
        Put32(glyphs.Loca, static_cast<std::uint32_t>(glyphs.Glyf.size()));

        auto cmap = MakeCmap(chosen);
        if (cmap.empty()) return {};

        std::array<std::pair<char const*, std::vector<unsigned char>>, 7> const tables{ {
            { "cmap", std::move(cmap) },
            { "glyf", std::move(glyphs.Glyf) },
            { "head", MakeHead(glyphs) },
            { "hhea", MakeHhea(font, glyphs) },
            { "hmtx", std::move(glyphs.Hmtx) },
            { "loca", std::move(glyphs.Loca) },
            { "maxp", MakeMaxp(glyphs) },
        } };
        return Assemble(tables);
    }

    Layout ReadLayout(std::span<unsigned char const> movie, std::string_view fontName, std::span<char32_t const> characters)
    {
        FontTables font;
        if (!FindFont(movie, fontName, font)) return {};

        Layout layout;
        layout.SizePerEm = static_cast<float>(font.Ascent + font.Descent) / SwfUnitsPerEm;

        ByteReader reader(font.Trailer);
        for (std::size_t i = 0; i < font.Count && !reader.Failed(); ++i)
        {
            auto const bits = reader.U8() >> 3;
            reader.Take((5 + 4 * bits + 7) / 8 - 1);
        }
        std::size_t const pairs = reader.U16();
        auto const table = reader.Take(pairs * 6);
        if (reader.Failed()) return layout;

        auto const wanted = ToSet(characters);
        for (std::size_t i = 0; i < pairs; ++i)
        {
            auto const left = U16At(table, i * 3);
            auto const right = U16At(table, i * 3 + 1);
            if (left >= wanted.size() || right >= wanted.size() || !wanted[left] || !wanted[right]) continue;
            auto const adjustment = static_cast<std::int16_t>(static_cast<std::uint16_t>(U16At(table, i * 3 + 2)));
            layout.Kerning.push_back({ left, right, static_cast<float>(adjustment) / SwfUnitsPerEm });
        }
        return layout;
    }
}
