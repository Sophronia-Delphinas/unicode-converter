#include <iostream>
#include <fstream>
#include <Windows.h>

bool CheckIfUtf8(std::fstream& in) {
	std::streampos pos = in.tellg();

	in.seekg(0, std::ios::beg);
	int32_t header = 0;
	for (int i = 0; i < 3 && !in.eof(); i++) {
		BYTE c = in.peek();
		header <<= 8;
		header |= c;
		in.seekg(1, std::ios::cur);
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
	BYTE c = in.peek();
	in.seekg(1, std::ios::cur);
	int size = SizeOfUnicode(c);
	if (!size)
		return c;
	if (size == 8)
		return 0;
	int32_t ret = c & (0xFF >> (7 - size));
	for (int i = 0; i < size - 1; i++) {
		c = in.peek();
		ret <<= 8;
		ret |= c & 0x3F;

		in.seekg(1, std::ios::cur);
	}
	return ret;
}

int main() {
	std::fstream in("source.txt");
	std::fstream out("target.txt");

	if (CheckIfUtf8(in)) {
		int32_t c = -1;
		while (!in.eof() && c) {
			c = PeekFromUtf8(in);
			if (c == '\n')
				in.seekg(1, std::ios::cur);
			std::cout << c << std::endl;
		}
	}

	return 0;
}
