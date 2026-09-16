#include "new_crc8.h"

unsigned char update_crc( unsigned char b, unsigned char init_crc );


unsigned char crc( unsigned char *command, unsigned char num_byte_pack )
{
	unsigned char i;
	unsigned char init_crc = 0;
        
	for( i=0; i<num_byte_pack; ++i )
	{
		init_crc = update_crc( command[i], init_crc );
	}
	return init_crc;
}  

unsigned char update_crc( unsigned char b, unsigned char init_crc )
{
    unsigned char i;
    unsigned char crc = init_crc;
    i=8;
    do
    {
        if ((b ^ crc) & 0x80)
            crc = (crc << 1) ^ 0xA9;
        else
            crc = crc << 1;
        b<<=1;
        i--;
    }while(i != 0 );
    return crc;
}/*** updateCRC8 ***/