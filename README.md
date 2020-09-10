# PMap
A persistent concurrent hash map

## Project TODO

\* means done

\> means prioritize

	Standardize performance tests.
		Tests:
			Type:
				Node Degree: Not supported by most (ours works)
				Node connected: Supported by all
			RMAT test
			reddit real-world test
			Random ops ("fuzzing" test)
			Create persistence tests with synthetic power failures.
		Implementations:
			Mine:
				*w/wo resize
				linear/hopscotch
				*volatile/NVM
				blocking/nonblocking?
			Alternatives:
				OneFile
				PMDK libpmemobj concurrent hash map
				*STL hash map (coarse-grained locks)
	Fix resizing.
	Fix KCAS.
		Implement hopscotch hashing.
	Implement suffix function to report results.
	Run initial performance tests to report.
		Set up and run on Intel SDP system.
	First 3 sections.
		Find target venues for short papers.