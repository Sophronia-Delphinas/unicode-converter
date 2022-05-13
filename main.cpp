#include <iostream>
#include <fstream>
#include <sstream>
#include <Windows.h>
#include <array>
#include <vector>

namespace cbm::core {
    using UNICHAR = uint32_t;
    using USTR = UNICHAR*;
    using CUSTR = const USTR;
}
namespace cbm::util {
    using namespace cbm::core;

    enum ENDIAN {
        BIG_ENDIAN = 1,
        LITTLE_ENDIAN = -1,
        UNKNOWN = 0
    };

    /**
     * @return 1 if big-endian; -1 if little-endian
     */
    ENDIAN GetEndian() {
        union uu {
            int i;
            char c;
        } u{};
        u.i = 1;
        return u.c ? LITTLE_ENDIAN : BIG_ENDIAN;
    }

    class CODE_CONVERTER;
	class I_CODE_CONVERTER {
    public:
        I_CODE_CONVERTER() = default;
        virtual ~I_CODE_CONVERTER() = default;

        /**
         * Checks the bom of input stream
         * @return 1 if big-endian; -1 if little-endian; 0 if format incorrect or unknown
         */
        virtual ENDIAN CheckBOM(std::istream& in) = 0;
        virtual void WriteBOM(std::ostream &out, ENDIAN endian) = 0;
        virtual bool EndianLegal(ENDIAN endian) = 0;

        virtual UNICHAR PeekFromStream(std::istream &in, ENDIAN endian) = 0;

        std::vector<UNICHAR> ConvertString(const char* str, ENDIAN endian) {
            std::vector<UNICHAR> vector;
            std::stringstream source(str);

            while (true) {
                UNICHAR uc = PeekFromStream(source, endian);
                if (!uc)
                    break;
                vector.push_back(uc);
            }

            return vector;
        }

        virtual int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, ENDIAN endian) = 0;
        virtual void WriteToStream(std::ostream &out, UNICHAR c, ENDIAN endian) = 0;
        void WriteUString(std::ostream& out, CUSTR uString, ENDIAN endian) {
            for (size_t i = 0; uString[i]; i++)
                WriteToStream(out, uString[i], endian);
        }
	};
    class CODE_CONVERTER : public I_CODE_CONVERTER {
        I_CODE_CONVERTER* converter;
    public:
        CODE_CONVERTER();
        CODE_CONVERTER(const CODE_CONVERTER&) = delete;
        CODE_CONVERTER(CODE_CONVERTER&& another) noexcept {
            converter = another.converter;
            another.converter = nullptr;
        }
        explicit CODE_CONVERTER(I_CODE_CONVERTER* iCodeConverter) {
            converter = iCodeConverter;
        }
        ~CODE_CONVERTER() override {
            delete converter;
        }

        CODE_CONVERTER& operator= (const CODE_CONVERTER&) = delete;
        CODE_CONVERTER& operator= (CODE_CONVERTER&& another) noexcept {
            if (this != &another) {
                converter = another.converter;
                another.converter = nullptr;
            }
            return *this;
        }

        ENDIAN CheckBOM(std::istream& in) override {
            return converter->CheckBOM(in);
        }
        void WriteBOM(std::ostream &out, ENDIAN endian) override {
            converter->WriteBOM(out, endian);
        }
        bool EndianLegal(ENDIAN endian) override {
            return converter->EndianLegal(endian);
        }

        UNICHAR PeekFromStream(std::istream &in, ENDIAN endian) override {
            return converter->PeekFromStream(in, endian);
        }

        int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, ENDIAN endian) override {
            return converter->ConvertUnicode(c, u8Array, endian);
        }
        void WriteToStream(std::ostream &out, UNICHAR c, ENDIAN endian) override {
            converter->WriteToStream(out, c, endian);
        }

        [[maybe_unused]] static CODE_CONVERTER BaseUTF8();
        [[maybe_unused]] static CODE_CONVERTER BaseUTF16();
    };

    class CONV_UTF8 : public I_CODE_CONVERTER {
        static int SizeOfUnicode(BYTE first) {
            char size = 0;
            for (BYTE t = first; t & 0x80; t <<= 1, size++);
            return size;
        }
    public:
        CONV_UTF8() = default;
        ~CONV_UTF8() override = default;

        ENDIAN CheckBOM(std::istream& in) override {\
            int32_t bom = 0;
            for (int i = 0; i < 3 && !in.eof(); i++) {
                char c;
                in.get(c);
                bom <<= 8;
                bom |= (BYTE)c;
            }

            if (bom == 0xEFBBBF)
                return GetEndian();
            else {
                in.seekg(0, std::ios::beg);
                return UNKNOWN;
            }
        }
        void WriteBOM(std::ostream &out, ENDIAN endian) override {
            out << (BYTE)0xEF << (BYTE)0xBB << (BYTE)0xBF;
        }
        bool EndianLegal(ENDIAN endian) override {
            return true;
        }

        UNICHAR PeekFromStream(std::istream &in, ENDIAN endian) override {
            std::istream::int_type check = in.peek();
            if (check == EOF)
                return 0;
            char c = (char)in.get();
            int size = SizeOfUnicode(c);
            if (!size)
                return (BYTE)c;
            if (size == 8)
                return 0;
            int32_t ret = ((BYTE)c) & (0xFF >> (7 - size));
            for (int i = 0; i < size - 1; i++) {
                in.get(c);
                if (c == EOF)
                    return 0;
                ret <<= 6;
                ret |= ((BYTE)c) & 0x3F;
            }
            return ret;
        }

        int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, ENDIAN endian) override {
            if (c <= 0x7F) {
                u8Array[0] = (BYTE)c;
                return 1;
            }
            int size;
            if (c <= 0x7FF)
                size = 2;
            else if (c <= 0xFFFF)
                size = 3;
            else
                size = 4;
            for (int i = size - 1; i > 0; i--) {
                BYTE tail = c & 0x3F;
                u8Array[i] = (BYTE)(0x80 | tail);
                c >>= 6;
            }
            BYTE mask = (0xFF >> (8 - size)) << (8 - size);
            u8Array[0] = (BYTE)(mask | (BYTE)c);
            return size;
        }
        void WriteToStream(std::ostream &out, UNICHAR c, ENDIAN endian) override {
            std::array<BYTE, 4> buffer = {};
            int size = ConvertUnicode(c, buffer, endian);
            for (int i = 0; i < size; i++)
                out << buffer[i];
        }
    };
    class CONV_UTF16 : public I_CODE_CONVERTER {
        static bool GetBytes(std::istream &in, std::array<BYTE, 4> &container) {
            for (size_t i = 0; i < 4; i++) {
                char c;
                in.get(c);
                if (c == EOF)
                    return false;
                container[i] = c;
            }
            return true;
        }
        static bool IsBasic(WORD w1, WORD w2) {
            return (w1 >> 10) != 0x36 || (w2 >> 10) != 0x37;
        }
        static WORD BuildWord(BYTE b1, BYTE b2, ENDIAN endian) {
            if (endian == BIG_ENDIAN)
                return (b1 << 8) | b2;
            else
                return (b2 << 8) | b1;
        }
        static void SplitWord(WORD w, std::array<BYTE, 2>& byteArray, ENDIAN endian) {
            BYTE h = w >> 8;
            BYTE l = w & 0xFF;
            if (endian == BIG_ENDIAN) {
                byteArray[0] = h;
                byteArray[1] = l;
            }
            else {
                byteArray[0] = l;
                byteArray[1] = h;
            }
        }
        static UNICHAR BuildUniChar(WORD w1, WORD w2) {
            return ((w1 & (0xFFFF >> 10)) << 10) | (w2 & (0xFFFF >> 10)) + 0x10000;
        }
    public:
        CONV_UTF16() = default;
        ~CONV_UTF16() override = default;

        ENDIAN CheckBOM(std::istream& in) override {
            in.seekg(0, std::ios::beg);
            int32_t bom = 0;
            for (int i = 0; i < 2 && !in.eof(); i++) {
                char c;
                in.get(c);
                bom <<= 8;
                bom |= (BYTE)c;
            }

            if (bom == 0xFFFE)
                return LITTLE_ENDIAN;
            else if (bom == 0xFEFF)
                return BIG_ENDIAN;
            else
                return UNKNOWN;
        }
        bool EndianLegal(ENDIAN endian) override {
            return endian != UNKNOWN;
        }

        UNICHAR PeekFromStream(std::istream &in, ENDIAN endian) override {
            std::istream::int_type check = in.peek();
            if (check == EOF)
                return 0;
            std::array<BYTE, 4> buffer{};
            if (!GetBytes(in, buffer))
                return 0;
            WORD w1 = BuildWord(buffer[0], buffer[1], endian);
            WORD w2 = BuildWord(buffer[2], buffer[3], endian);
            bool basic = IsBasic(w1, w2);
            if (basic) {
                in.seekg(-2, std::ios::cur);
                return w1;
            }
            else
                return BuildUniChar(w1, w2);
        }

        int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, ENDIAN endian) override {
            std::array<BYTE, 2> buffer{};
            if (c > 0xFFFF) {
                c -= 0x10000;
                WORD h, l;
                if (endian == LITTLE_ENDIAN) {
                    h = (c >> 10) | 0xD800;
                    l = (c & 0x03FF) | 0xDC00;
                }
                else {
                    h = (c & 0x03FF) | 0xD800;
                    l = (c >> 10) | 0xDC00;
                }
                SplitWord(h, buffer, endian);
                memcpy(&u8Array[0], &buffer[0], 2);
                SplitWord(l, buffer, endian);
                memcpy(&u8Array[2], &buffer[0], 2);
                return 4;
            }
            else {
                SplitWord(c, buffer, endian);
                memcpy(&u8Array[0], &buffer[0], 2);
                return 2;
            }
        }
        void WriteBOM(std::ostream &out, ENDIAN endian) override {
            if (endian == BIG_ENDIAN)
                out << (BYTE)0xFE << (BYTE)0xFF;
            else
                out << (BYTE)0xFF << (BYTE)0xFE;
        }
        void WriteToStream(std::ostream &out, UNICHAR c, ENDIAN endian) override {
            std::array<BYTE, 4> buffer = {};
            int size = ConvertUnicode(c, buffer, endian);
            for (int i = 0; i < size; i++)
                out << buffer[i];
        }
    };

    CODE_CONVERTER::CODE_CONVERTER() {
        converter = new CONV_UTF8;
    }

    [[maybe_unused]] CODE_CONVERTER CODE_CONVERTER::BaseUTF8() {
        return CODE_CONVERTER(new CONV_UTF8);
    }
    [[maybe_unused]] CODE_CONVERTER CODE_CONVERTER::BaseUTF16() {
        return CODE_CONVERTER(new CONV_UTF16);
    }
}
using namespace cbm::util;

int main() {
	std::fstream in("../source-u16.txt", std::ios::in | std::ios::binary);
	std::fstream out("../target-u16.txt", std::ios::out | std::ios::binary);

    auto converter = CODE_CONVERTER::BaseUTF16();
    ENDIAN systemEndian = GetEndian();
    converter.WriteBOM(out, systemEndian);

    ENDIAN inputEndian = converter.CheckBOM(in);
    if (!converter.EndianLegal(inputEndian))
        return 0;

    while (!in.eof()) {
        UNICHAR c = converter.PeekFromStream(in, inputEndian);
        if (c == 0)
            break;
        std::cout << c << std::endl;
        converter.WriteToStream(out, c, systemEndian);
    }

	return 0;
}
