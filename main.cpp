#include <iostream>
#include <fstream>
#include <Windows.h>

class EndOfFileException : public std::exception {

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
	int prevSize = 7 - size;
	int32_t ret = ((BYTE)c) & (0xFF >> prevSize);
	for (int i = 0; i < size - 1; i++) {
		in.get(c);
		if (c == EOF)
			throw EndOfFileException();
		ret <<= prevSize;
		ret |= ((BYTE)c) & 0x3F;
		prevSize = 6;
	}
	return ret;
}

int main() {
	std::fstream in("source.txt");
	std::fstream out("target.txt");

	if (CheckIfUtf8(in)) {
		while (!in.eof()) {
			int32_t c = PeekFromUtf8(in);
			if (c == 0)
				break;
			std::cout << c << std::endl;
		}
	}

	return 0;
}
