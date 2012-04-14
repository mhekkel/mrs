#include "M6Lib.h"

#include <limits>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>

#include "M6SequenceFilter.h"

using namespace std;

class M6Alphabet
{
  public:
					M6Alphabet(const char* inChars);
	
	bool			Contains(char inChar) const;
	long			GetIndex(char inChar) const;
	long			GetSize() const					{ return mAlphaSize; }
	double			GetLnSize() const				{ return mAlphaLnSize; }

  private:
	long			mAlphaSize;
	double			mAlphaLnSize;
	long			mAlphaIndex[128];
	const char*		mAlphaChars;
};

M6Alphabet::M6Alphabet(const char* inChars)
	: mAlphaChars(inChars)
{
	mAlphaSize = static_cast<int>(strlen(inChars));
	mAlphaLnSize = log(static_cast<double>(mAlphaSize));
	
	for (uint32 i = 0; i < 128; ++i)
	{
		mAlphaIndex[i] =
			static_cast<long>(find(mAlphaChars, mAlphaChars + mAlphaSize, toupper(i)) - mAlphaChars);
	}
}

bool M6Alphabet::Contains(char inChar) const
{
	bool result = false;
	if (inChar >= 0)
		result = mAlphaIndex[toupper(inChar)] < mAlphaSize;
	return result;
}

long M6Alphabet::GetIndex(char inChar) const
{
	return mAlphaIndex[toupper(inChar)];
}

const M6Alphabet
	kProtAlphabet = M6Alphabet("ACDEFGHIKLMNPQRSTVWY"),
	kNuclAlphabet = M6Alphabet("ACGTU");

class M6Window
{
  public:
			M6Window(const string& inSequence, long inStart, long inLength, const M6Alphabet& inAlphabet);

	void	CalcEntropy();
	bool	ShiftWindow();
	
	double	GetEntropy() const			{ return mEntropy; }
	long	GetBogus() const			{ return mBogus; }
	
	void	DecState(long inCount);
	void	IncState(long inCount);
	
	void	Trim(long& ioEndL, long& ioEndR, long inMaxTrim);

  private:
	const string&	mSequence;
	vector<long>	mComposition;
	vector<long>	mState;
	long			mStart;
	long			mLength;
	long			mBogus;
	double			mEntropy;
	const M6Alphabet&mAlphabet;
};

M6Window::M6Window(const string& inSequence, long inStart, long inLength, const M6Alphabet& inAlphabet)
	: mSequence(inSequence)
	, mComposition(inAlphabet.GetSize())
	, mStart(inStart)
	, mLength(inLength)
	, mBogus(0)
	, mEntropy(-2.0)
	, mAlphabet(inAlphabet)
{
	long alphaSize = mAlphabet.GetSize();
	
	for (long i = mStart; i < mStart + mLength; ++i)
	{
		if (mAlphabet.Contains(mSequence[i]))
			++mComposition[mAlphabet.GetIndex(mSequence[i])];
		else
			++mBogus;
	}
	
	mState.insert(mState.begin(), alphaSize + 1, 0);

	int n = 0;
	for (long i = 0; i < alphaSize; ++i)
	{
		if (mComposition[i] > 0)
		{
			mState[n] = mComposition[i];
			++n;
		}
	}
	
	sort(mState.begin(), mState.begin() + n, greater<long>());
}

void M6Window::CalcEntropy()
{
	mEntropy = 0.0;

	double total = 0.0;
	for (uint32 i = 0; i < mState.size() and mState[i] != 0; ++i)
		total += mState[i];

	if (total != 0.0)
	{
		for (uint32 i = 0; i < mState.size() and mState[i]; ++i)
		{
			double t = mState[i] / total;
			mEntropy += t * log(t);
		}
		mEntropy = fabs(mEntropy / log(0.5));
	}
}

void M6Window::DecState(long inClass)
{
	for (uint32 ix = 0; ix < mState.size() and mState[ix] != 0; ++ix)
	{
		if (mState[ix] == inClass and mState[ix + 1] < inClass)
		{
			--mState[ix];
			break;
		}
	}
}

void M6Window::IncState(long inClass)
{
	for (uint32 ix = 0; ix < mState.size(); ++ix)
	{
		if (mState[ix] == inClass)
		{
			++mState[ix];
			break;
		}
	}
}

bool M6Window::ShiftWindow()
{
	if (uint32(mStart + mLength) >= mSequence.length())
		return false;
	
	char ch = mSequence[mStart];
	if (mAlphabet.Contains(ch))
	{
		long ix = mAlphabet.GetIndex(ch);
		DecState(mComposition[ix]);
		--mComposition[ix];
	}
	else
		--mBogus;
	
	++mStart;
	
	ch = mSequence[mStart + mLength - 1];
	if (mAlphabet.Contains(ch))
	{
		long ix = mAlphabet.GetIndex(ch);
		IncState(mComposition[ix]);
		++mComposition[ix];
	}
	else
		++mBogus;

	if (mEntropy > -2.0)
		CalcEntropy();
	
	return true;
}

static double lnfac(long inN)
{
    const double c[] = {
         76.18009172947146,
        -86.50532032941677,
         24.01409824083091,
        -1.231739572450155,
         0.1208650973866179e-2,
        -0.5395239384953e-5
    };
	static map<long,double> sLnFacMap;
	
	if (sLnFacMap.find(inN) == sLnFacMap.end())
	{
		double x = inN + 1;
		double t = x + 5.5;
		t -= (x + 0.5) * log(t);
	    double ser = 1.000000000190015; 
	    for (int i = 0; i <= 5; i++)
	    {
	    	++x;
	        ser += c[i] / x;
	    }
	    sLnFacMap[inN] = -t + log(2.5066282746310005 * ser / (inN + 1));
	}
	
	return sLnFacMap[inN];
}

static double lnperm(vector<long>& inState, long inTotal)
{
	double ans = lnfac(inTotal);
	for (uint32 i = 0; i < inState.size() and inState[i] != 0; ++i)
		ans -= lnfac(inState[i]);
	return ans;
}

static double lnass(vector<long>& inState, M6Alphabet inAlphabet)
{
    double result = lnfac(inAlphabet.GetSize());
    if (inState.size() == 0 or inState[0] == 0)
        return result;
    
    int total = inAlphabet.GetSize();
    int cl = 1;
    int i = 1;
    int sv_cl = inState[0];
    
    while (inState[i] != 0)
    {
        if (inState[i] == sv_cl) 
            cl++;
        else
        {
            total -= cl;
            result -= lnfac(cl);
            sv_cl = inState[i];
            cl = 1;
        }
        i++;
    }

    result -= lnfac(cl);
    total -= cl;
    if (total > 0)
    	result -= lnfac(total);
    
    return result;
}

static double lnprob(vector<long>& inState, long inTotal, const M6Alphabet& inAlphabet)
{
	double ans1, ans2 = 0, totseq;

	totseq = inTotal * inAlphabet.GetLnSize();
	ans1 = lnass(inState, inAlphabet);
	if (ans1 > -100000.0 and inState[0] != numeric_limits<long>::min())
		ans2 = lnperm(inState, inTotal);
	else
		cerr << "Error in calculating lnass" << endl;
	return ans1 + ans2 - totseq;
}

void M6Window::Trim(long& ioEndL, long& ioEndR, long inMaxTrim)
{
	double minprob = 1.0;
	long lEnd = 0;
	long rEnd = mLength - 1;
	int minLen = 1;
	int maxTrim = inMaxTrim;
	if (minLen < mLength - maxTrim)
		minLen = mLength - maxTrim;
	
	for (long len = mLength; len > minLen; --len)
	{
		M6Window w(mSequence, mStart, len, mAlphabet);
		
		int i = 0;
		bool shift = true;
		while (shift)
		{
			double prob = lnprob(w.mState, len, mAlphabet);
			if (prob < minprob)
			{
				minprob = prob;
				lEnd = i;
				rEnd = len + i - 1;
			}
			shift = w.ShiftWindow();
			++i;
		}
	}

	ioEndL += lEnd;
	ioEndR -= mLength - rEnd - 1;
}

static bool GetEntropy(const string& inSequence, const M6Alphabet& inAlphabet,
	long inWindow, long inMaxBogus, vector<double>& outEntropy)
{
	bool result = false;

	long downset = (inWindow + 1) / 2 - 1;
	long upset = inWindow - downset;
	
	if (inWindow <= inSequence.length())
	{
		result = true;
		outEntropy.clear();
		outEntropy.insert(outEntropy.begin(), inSequence.length(), -1.0);
		
		M6Window win(inSequence, 0, inWindow, inAlphabet);
		win.CalcEntropy();
		
		long first = downset;
		long last = static_cast<long>(inSequence.length() - upset);
		for (long i = first; i <= last; ++i)
		{
//			if (GetPunctuation() and win.HasDash())
//			{
//				win.ShiftWindow();
//				continue;
//			}
			if (win.GetBogus() > inMaxBogus)
				continue;
			
			outEntropy[i] = win.GetEntropy();
			win.ShiftWindow();
		}
	}
	
	return result;
}

static void GetMaskSegments(bool inProtein, const string& inSequence, long inOffset,
	vector<pair<long,long> >& outSegments)
{
	double loCut, hiCut;
	long window, maxbogus, maxtrim;
	bool overlaps;
	const M6Alphabet* alphabet;

	if (inProtein)
	{
		window = 12;
		loCut = 2.2;
		hiCut = 2.5;
		overlaps = false;
		maxtrim = 50;
		maxbogus = 2;
		alphabet = &kProtAlphabet;
	}
	else
	{
		window = 32;
		loCut = 1.4;
		hiCut = 1.6;
		overlaps = false;
		maxtrim = 100;
		maxbogus = 3;
		alphabet = &kNuclAlphabet;
	}

	long downset = (window + 1) / 2 - 1;
	long upset = window - downset;
	
	vector<double> e;
	GetEntropy(inSequence, *alphabet, window, maxbogus, e);

	long first = downset;
	long last = static_cast<long>(inSequence.length() - upset);
	long lowlim = first;

	for (long i = first; i <= last; ++i)
	{
		if (e[i] <= loCut and e[i] != -1.0)
		{
			long loi = i;
			while (loi >= lowlim and e[loi] != -1.0 and e[loi] <= hiCut)
				--loi;
			++loi;
			
			long hii = i;
			while (hii <= last and e[hii] != -1.0 and e[hii] <= hiCut)
				++hii;
			--hii;
			
			long leftend = loi - downset;
			long rightend = hii + upset - 1;
			
			string s(inSequence.substr(leftend, rightend - leftend + 1));
			M6Window w(s, 0, rightend - leftend + 1, *alphabet);
			w.Trim(leftend, rightend, maxtrim);

			if (i + upset - 1 < leftend)
			{
				long lend = loi - downset;
				long rend = leftend - 1;
				
				string left(inSequence.substr(lend, rend - lend + 1));
				GetMaskSegments(inProtein, left, inOffset + lend, outSegments);
			}
			
			outSegments.push_back(
				pair<long,long>(leftend + inOffset, rightend + inOffset + 1));
			i = rightend + downset;
			if (i > hii)
				i = hii;
			lowlim = i + 1;
		}
	}
}

string SEG(const string& inSequence)
{
	string result = inSequence;
	
	vector<pair<long,long> > segments;
	GetMaskSegments(true, result, 0, segments);
	
	for (uint32 i = 0; i < segments.size(); ++i)
	{
		for (long j = segments[i].first; j < segments[i].second; ++j)
			result[j] = 'X';
	}
	
	return result;
}

string DUST(const string& inSequence)
{
	string result = inSequence;
	
	vector<pair<long,long> > segments;
	GetMaskSegments(false, inSequence, 0, segments);
	
	for (uint32 i = 0; i < segments.size(); ++i)
	{
		for (long j = segments[i].first; j < segments[i].second; ++j)
			result[j] = 'X';
	}
	
	return result;
}

//int main()
//{
//	string seq;
//	
//	ifstream in("input.seq", ios::binary);
//	in >> seq;
//	cout << FilterProtSeq(seq);
//	return 0;
//}
