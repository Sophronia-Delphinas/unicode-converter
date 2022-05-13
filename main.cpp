#include <iostream>
#include <fstream>
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

        virtual UNICHAR PeekFromString(const void *source, size_t &cursor, ENDIAN endian) = 0;
        std::vector<UNICHAR> ConvertString(const void* str, ENDIAN endian) {
            std::vector<UNICHAR> vector;

            size_t cursor = 0;
            while (true) {
                UNICHAR uc = PeekFromString(str, cursor, endian);
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
        UNICHAR PeekFromString(const void *source, size_t &cursor, ENDIAN endian) override {
            return converter->PeekFromString(source, cursor, endian);
        }

        int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, ENDIAN endian) override {
            return converter->ConvertUnicode(c, u8Array, endian);
        }
        void WriteToStream(std::ostream &out, UNICHAR c, ENDIAN endian) override {
            converter->WriteToStream(out, c, endian);
        }

        static CODE_CONVERTER BaseUTF8();
        static CODE_CONVERTER BaseUTF16();
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
            char c;
            in.get(c);
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
        UNICHAR PeekFromString(const void *source, size_t &cursor, ENDIAN endian) override {
            const BYTE* str = static_cast<const BYTE *>(source);
            if (!str[cursor])
                return 0;
            BYTE c = str[cursor++];
            int size = SizeOfUnicode(c);
            if (!size)
                return c;
            if (size == 8)
                return 0;
            int32_t ret = c & (0xFF >> (7 - size));
            for (int i = 0; i < size - 1; i++) {
                c = str[cursor++];
                if (!c)
                    return 0;
                ret <<= 6;
                ret |= c & 0x3F;
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
        static bool IsBasic(WORD w1, WORD w2) {
            return (w1 >> 10) != 0x36 || (w2 >> 10) != 0x37;
        }
        static WORD BuildWord(BYTE b1, BYTE b2, bool endian) {
            if (endian)
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
            BYTE b1 = in.get();
            BYTE b2 = in.get();
            BYTE b3 = in.get();
            BYTE b4 = in.get();
            WORD w1 = BuildWord(b1, b2, endian);
            WORD w2 = BuildWord(b3, b4, endian);
            bool basic = IsBasic(w1, w2);
            if (basic) {
                in.seekg(-1, std::ios::cur);
                return w1;
            }
            else
                return ((w1 >> 10) & 0x03FF) | (w2 & 0x03FF);
        }
        UNICHAR PeekFromString(const void *source, size_t &cursor, ENDIAN endian) override {
            const BYTE* str = static_cast<const BYTE *>(source);
            if (!str[cursor])
                return 0;
            BYTE c1 = str[cursor++];
            BYTE c2 = str[cursor++];
            BYTE c3 = str[cursor++];
            BYTE c4 = str[cursor++];
            WORD w1 = BuildWord(c1, c2, endian);
            WORD w2 = BuildWord(c3, c4, endian);
            bool basic = IsBasic(w1, w2);
            if (basic) {
                cursor -= 2;
                return w1;
            }
            else
                return ((w1 >> 10) & 0x03FF) | (w2 & 0x03FF);
        }

        int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, ENDIAN endian) override {
            std::array<BYTE, 2> byteArray{};
            if (c > 0xFFFF) {

            }
            else {
                SplitWord(c, byteArray, endian);
            }

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
    CODE_CONVERTER CODE_CONVERTER::BaseUTF8() {
        return CODE_CONVERTER(new CONV_UTF8);
    }
    CODE_CONVERTER CODE_CONVERTER::BaseUTF16() {
        return CODE_CONVERTER(new CONV_UTF16);
    }
}
using namespace cbm::util;

int main() {
	std::fstream in("../source-u16.txt");
	std::fstream out("../target-u16.txt", std::ios::out);

    auto converter = CODE_CONVERTER::BaseUTF16();
    converter.WriteBOM(out, GetEndian());

    ENDIAN endian = converter.CheckBOM(in);
    if (!converter.EndianLegal(endian))
        return 0;

    while (!in.eof()) {
        UNICHAR c = converter.PeekFromStream(in, endian);
        if (c == 0)
            break;
        std::cout << c << std::endl;
//            converter.WriteToStream(out, c, UNKNOWN);
    }

	return 0;
}
