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

	class CODE_CONVERTER {
    protected:
        CODE_CONVERTER() = default;
        virtual ~CODE_CONVERTER() = default;
    public:
        virtual int CheckStreamHeader(std::istream& in) {
            throw std::exception();
        }
        virtual UNICHAR PeekFromStream(std::istream &in, bool endian) {
            throw std::exception();
        }

        virtual UNICHAR PeekFromString(const void *source, size_t &cursor, bool endian) = 0;
        std::vector<UNICHAR> ConvertString(const void* str, bool endian) {
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

        virtual int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, bool endian) = 0;
        virtual void WriteToStream(std::ostream &out, UNICHAR c, bool endian) {
            throw std::exception();
        }
        void WriteUString(std::ostream& out, CUSTR uString, bool endian) {
            for (size_t i = 0; uString[i]; i++)
                WriteToStream(out, uString[i], endian);
        }

        static CODE_CONVERTER& BaseUTF8Converter();
	};

    class CONV_UTF8 : public CODE_CONVERTER {
        static int SizeOfUnicode(BYTE first) {
            char size = 0;
            for (BYTE t = first; t & 0x80; t <<= 1, size++);
            return size;
        }
    public:
        CONV_UTF8() = default;
        ~CONV_UTF8() override = default;

        int CheckStreamHeader(std::istream& in) override {
            std::streampos pos = in.tellg();

            in.seekg(0, std::ios::beg);
            int32_t header = 0;
            for (int i = 0; i < 3 && !in.eof(); i++) {
                char c;
                in.get(c);
                header <<= 8;
                header |= (BYTE)c;
            }

            bool ret = header == 0xEFBBBF;
            if (!ret)
                in.seekg(pos);

            return ret;
        }
        UNICHAR PeekFromStream(std::istream &in, bool endian) override {
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
        UNICHAR PeekFromString(const void *source, size_t &cursor, bool endian) override {
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

        int ConvertUnicode(UNICHAR c, std::array<BYTE, 4> &u8Array, bool endian) override {
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
        void WriteToStream(std::ostream &out, UNICHAR c, bool endian) override {
            std::array<BYTE, 4> buffer = {};
            int size = ConvertUnicode(c, buffer, endian);
            for (int i = 0; i < size; i++)
                out << buffer[i];
        }
    };

    CODE_CONVERTER& CODE_CONVERTER::BaseUTF8Converter() {
        static CONV_UTF8 converter;
        return converter;
    }
}
using namespace cbm::util;

int main() {
	std::fstream in("../source.txt");
	std::fstream out("../target.txt", std::ios::out);
	out << (BYTE)0xEF << (BYTE)0xBB << (BYTE)0xBF;

    auto& converterU8 = CODE_CONVERTER::BaseUTF8Converter();

	if (converterU8.CheckStreamHeader(in)) {
		while (!in.eof()) {
            UNICHAR c = converterU8.PeekFromStream(in, true);
			if (c == 0)
				break;
			std::cout << c << std::endl;
            converterU8.WriteToStream(out, c, true);
		}
	}

	return 0;
}
