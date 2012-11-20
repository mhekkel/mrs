#pragma once

#ifndef SIGQUIT
#define SIGQUIT SIGTERM
#endif

#ifndef SIGHUP
#define SIGHUP SIGBREAK
#endif

class M6SignalCatcher
{
  public:
				M6SignalCatcher();
				~M6SignalCatcher();

	void		BlockSignals();
	void		UnblockSignals();
	
	int			WaitForSignal();

	static void	Signal(int inSignal);

  private:
				M6SignalCatcher(const M6SignalCatcher&);
	M6SignalCatcher&
				operator=(const M6SignalCatcher&);

	struct M6SignalCatcherImpl*	mImpl;
};

void SetStdinEcho(bool inEnable);

