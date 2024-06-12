/* © 2014 Silicon Laboratories Inc.
 */
  li.QuadPart += t->interval*10000L;

  t->timeHi = li.HighPart;
  t->timeLo = li.LowPart;
}

int timer_expired(struct timer *t){
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  if(ft.dwHighDateTime > t->timeHi) return 1;
  if(ft.dwHighDateTime == t->timeHi && ft.dwLowDateTime > t->timeLo) return 1;
  return 0;
}
#endif

int SerialInit(const char* port) {
        DCB dcb;        
        COMMTIMEOUTS Timeouts;

		WCHAR    wport[64]; 
		MultiByteToWideChar( 0,0, port, strlen(port), wport, sizeof(wport)); 

        FillMemory(&dcb, sizeof(dcb), 0);
        dcb.DCBlength = sizeof(dcb);

        dcb.BaudRate = 115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;

        hComm = CreateFile(wport, GENERIC_READ | GENERIC_WRITE,0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);


        if (hComm == INVALID_HANDLE_VALUE)
        {
		    return 0;
        }

        if ( !SetCommState(hComm, &dcb) )
        {          
            return 0;
        }


        // Read operation will wait max SER_TIMEOUT (ms) for a character before returning
        // See also serRead()

        Timeouts.ReadIntervalTimeout=MAXDWORD;
        Timeouts.ReadTotalTimeoutMultiplier=MAXDWORD;
        Timeouts.ReadTotalTimeoutConstant=2;
        SetCommTimeouts(hComm,&Timeouts);

        return 1;
}


int SerialGetByte() {
	unsigned char c=0;
	int n;

	if(ReadFile(hComm, &c, 1, &n,NULL) && n > 0) {
		//printf("Got %x %i\n",c,n);
		return c;
	}
	else
	{
		//printf("xx\n");
		return -1;
	}
}

void SerialPutByte(unsigned char c) {
	int n;
	if(!WriteFile(hComm, &c, 1, &n, NULL))
		printf("Write fail");
}

int SerialCheck() {
	//char c;
	//DWORD n;
	//PeekNamedPipe(hComm,0,0,&n,0,0);	
	//return n>0;
	return 1;
}

void SerialFlush() {
//
}
