#include "app/translated_patches.h"
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3) return 2;
    try {translated_patches::Apply(argv[1],argv[2]);std::cout<<"Verified translated patches "<<translated_patches::Revision(argv[2])<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
