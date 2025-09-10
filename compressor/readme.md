```
g++ -o brotli_demo brotli_demo.cpp -lbrotlienc -lbrotlicommon -lbrotlidec
./brotli_demo

g++ zlib_demo.cpp -o zlib_demo -lz
./zlib_demo

g++ zlib_brotli_demo.cpp -o zlib_brotli_demo -lz -lbrotlienc -lbrotlicommon -lbrotlidec
./zlib_brotli_demo
```