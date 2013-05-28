
#include <exception>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <new>
#include <ctime>

using namespace std;	

struct CHN_Header
{
	short signature;
	unsigned short mca_number;
	unsigned short segment;
	unsigned short seconds;
	long int realtime;
	long int livetime;
	char startdate[8];
	char starttime[4];
	unsigned short channel_offset;
	unsigned short channel_count;
};

struct CHN_Footer
{
	short signature;
	short reserved;
	float eczi;
	float ecs;
	float ecqt;
	float pscz;
	float pscs;
	float pscqt;
	char reserved2[228];
	unsigned char det_desc_len;
	char det_desc[63];
	unsigned char samp_desc_len;
	char samp_desc[63];
	char reserved3[128];
};

const unsigned int SPECTRUM_SIZE = 8192;
bool valid_date = true;

inline std::string trim_right(const std::string &source , const std::string& t = " ")
{
	string str = source;
	return str.erase( str.find_last_not_of(t) + 1);
}

inline std::string trim_left( const std::string& source, const std::string& t = " ")
{
	string str = source;
	return str.erase(0 , source.find_first_not_of(t) );
}

inline std::string trim(const std::string& source, const std::string& t = " ")
{
	string str = source;
	return trim_left( trim_right( str , t) , t );
} 

void prepare_chn(CHN_Header &header, CHN_Footer &footer);
void prepare_spectrum(int* spectrum);
void parse_acquisition_date(CHN_Header &header, const string& line);
void do_parse_acquisition_date(CHN_Header &header, const string& line);
void parse_livetime(CHN_Header &header, const string& line);
void parse_realtime(CHN_Header &header, const string& line);
void parse_channels(int *spectrum, const string& line);
string strip_tags(const string& line);
string progname;

int main(int argc, char* argv[])
{	
	progname = argv[0];
	int retval = 0;

	try
	{		
		CHN_Header header;	
		CHN_Footer footer;
		prepare_chn(header, footer);		

		int *spectrum = new (nothrow) int[SPECTRUM_SIZE];
		if(!spectrum)
		{
			cerr << progname << ": Unable to allocate memory for spectrum" << endl;
			return 1;
		}
		prepare_spectrum(spectrum);	

		ifstream fin("CAN_REP.$$$");			
		if(!fin.good())
		{
			cerr << progname << ": Unable to open CAN_REP.$$$" << endl;
			return 1;
		}

		string line, raw_line;
		for(int i = 0; fin.good(); i++)
		{
			getline(fin, raw_line);
			line = trim(raw_line, " \t\r\n");
			if(!line.length()) 
				continue;
			switch(i)
			{
			case 0:
				parse_acquisition_date(header, line);
				break;
			case 1:
				parse_livetime(header, line);
				break;
			case 2:
				parse_realtime(header, line);
				break;
			default:
				parse_channels(spectrum, line);
				break;
			}		
		}
		fin.close();

		ofstream fout("SP_BUFF.$$$", ios::binary | ios::out);
		if(!fout.good())
		{
			cerr << progname << ": Unable to create output file SP_BUFF.$$$" << endl;
			return 1;
		}
		fout.write((char*)&header, sizeof(CHN_Header));
		for(int i=0; i<SPECTRUM_SIZE; i++)
			fout.write((char*)&spectrum[i], sizeof(int));		
		fout.write((char*)&footer, sizeof(CHN_Footer));
		fout.close();

		delete [] spectrum;		
	}
	catch(std::exception& e)
	{
		cerr << e.what() << endl;		
		retval = 1;
	}
	
	return retval;
}

void prepare_chn(CHN_Header &header, CHN_Footer &footer)
{
	memset((void*)&header, 0, sizeof(CHN_Header));
	memset((void*)&footer, 0, sizeof(CHN_Footer));

	header.signature = -1;
	header.mca_number = 1;
	header.seconds = 0;
	header.segment = 1;		
	header.channel_offset = 0;
	header.channel_count = SPECTRUM_SIZE;
}

void prepare_spectrum(int* spectrum)
{	
	memset((void*)spectrum, 0, SPECTRUM_SIZE * sizeof(int));	
}

bool check_valid_date(const string& line)
{
	//dd.mm.yyyy hh:mm:ss
	if(line.length() < 16)
		return false;

	int period = 0, colon = 0, space = 0;
	for(string::const_iterator i = line.begin(); i != line.end(); i++)
	{
		if(*i == '.')
			++period;
		else if(*i == ':')
			++colon;
		else if(*i == ' ')
			++space;
	}

	if(period != 2 || !colon || !space)
		return false;

	return true;
}

void parse_acquisition_date(CHN_Header &header, const string& line)
{	
	// 30.03.2011 09:24:45 
	//DDMMMYY*
	
	string s = strip_tags(line);
	if(!check_valid_date(s))
	{
		valid_date = false;
		return;
	}	

	do_parse_acquisition_date(header, s);	
}

void do_parse_acquisition_date(CHN_Header &header, const string& line)
{	
	// 30.03.2011 09:24:45 
	//DDMMMYY*			
	char* mnds[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
	int mnd, year;
	stringstream ss;
	ss << line.substr(3, 2).c_str();
	ss >> mnd;
	ss.clear();
	ss << line.substr(6, 4).c_str();
	ss >> year;
	strncpy(header.startdate, line.c_str(), 2);
	strncpy(header.startdate + 2, mnds[mnd-1], 3);
	strncpy(header.startdate + 5, line.c_str() + 8, 2);
	if(year >= 2000)
		strncpy(header.startdate + 7, "1", 1);
	else 
		strncpy(header.startdate + 7, " ", 1);

	strncpy(header.starttime, line.c_str() + 11, 2);
	strncpy(header.starttime + 2, line.c_str() + 14, 2);	
}

void parse_livetime(CHN_Header &header, const string& line)
{	
	float flivetime = 0.0;
	string s = strip_tags(line);
	if(!s.length())
		throw length_error(progname + ": Livetime not found");
	stringstream ss;
	ss << s;
	ss >> flivetime;
	flivetime *= 50.0f;
	header.livetime = (int)flivetime;	
}

void parse_realtime(CHN_Header &header, const string& line)
{
	int rt = 0;
	float frealtime = 0.0;
	string s = strip_tags(line);
	if(!s.length())
		throw length_error(progname + ": Realtime not found");
	stringstream ss;
	ss << s;
	ss >> frealtime;
	rt = (int)frealtime;
	frealtime *= 50.0f;	
	header.realtime = (int)frealtime;	

	if(!valid_date)
	{
		char buffer[80];
		time_t now = time(0);
		now -= rt;
		tm* localtm = localtime(&now);
		strftime(buffer, 80, "%d.%m.%Y %H:%M:%S", localtm);				
		do_parse_acquisition_date(header, string(buffer));	
	}
}

void parse_channels(int *spectrum, const string& line)
{
	static int idx = 0;
	int cnt;
	string s = strip_tags(line);
	stringstream ss;
	ss << s;	
	while(ss >> cnt)
	{		
		spectrum[idx++] = cnt;	
		if(idx > SPECTRUM_SIZE)
			throw length_error(progname + ": Spectrum has more than 8192 channels");
	}
}

string strip_tags(const string& line)
{		
	string::size_type start_pos = line.find_first_of(':');
	if(start_pos == string::npos)
		throw invalid_argument(progname + ": strip_tags: line has no tag (" + line + ")");
	return trim_left(line.substr(start_pos + 1), " \t\r\n");	
}