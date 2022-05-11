#include <iostream>
#include <fstream>
#include <Windows.h>
#include <array>

using UCHAR_32 = uint32_t;

class EndOfFileException : public std::exception {
public:
    EndOfFileException() = default;
    ~EndOfFileException() override = default;
    [[nodiscard]] const char* what() const noexcept override {
        return "Unexpected end of file";
    }
};
class IllegalUtf8Exception : public std::exception {
public:
    IllegalUtf8Exception() = default;
    ~IllegalUtf8Exception() override = default;
    [[nodiscard]] const char* what() const noexcept override {
        return "Illegal UTF8 character";
    }
};

bool CheckIfUtf8(std::fstream& in) {
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
int SizeOfUnicode(BYTE header) {
	char size = 0;
	for (BYTE t = header; t & 0x80; t <<= 1, size++);
	return size;
}
int32_t PeekFromUtf8(std::fstream& in) {
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
			throw EndOfFileException();
		ret <<= 6;
		ret |= ((BYTE)c) & 0x3F;
	}
	return ret;
}

int SizeOfUtf32(UCHAR_32 c, std::array<BYTE, 4>& u8Array) {
	if (c > 0x10FFFF)
		throw IllegalUtf8Exception();
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
void WriteToUtf8(std::fstream& out, UCHAR_32 c) {
	std::array<BYTE, 4> buffer = {};
	int size = SizeOfUtf32(c, buffer);
	for (int i = 0; i < size; i++)
		out << buffer[i];
}

int main() {
	std::fstream in("../source.txt");
	std::fstream out("../target.txt", std::ios::out);
	out << (BYTE)0xEF << (BYTE)0xBB << (BYTE)0xBF;

	if (CheckIfUtf8(in)) {
		while (!in.eof()) {
			int32_t c = PeekFromUtf8(in);
			if (c == 0)
				break;
			std::cout << c << std::endl;
			WriteToUtf8(out, c);
		}
	}

	return 0;
}
