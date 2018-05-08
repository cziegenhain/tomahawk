#ifndef ALGORITHM_LOAD_BALANCER_LD_H_
#define ALGORITHM_LOAD_BALANCER_LD_H_

#include "../support/MagicConstants.h"
#include "../support/helpers.h"
#include "../tomahawk/TomahawkReader.h"
#include "load_balancer_block.h"

namespace Tomahawk{

class LoadBalancerLD{
private:
	typedef LoadBalancerLD      self_type;
	typedef LoadBalancerBlock   value_type;
	typedef value_type&         reference;
	typedef const value_type&   const_reference;
	typedef value_type*         pointer;
	typedef const value_type*   const_pointer;
	typedef std::ptrdiff_t      difference_type;
	typedef std::size_t         size_type;
	typedef TomahawkReader      reader_type;

public:
	LoadBalancerLD();
	~LoadBalancerLD();

	bool getSelectedLoad();
	bool getSelectedLoadThreads(const reader_type& reader, const U32 threads);
	bool setSelected(const S32 selected);
	bool setDesired(const S32 desired);
	bool Build(const reader_type& reader, const U32 threads);
	bool BuildWindow(const reader_type& reader, const U32 threads);
	inline std::vector< std::pair<U32, U32> >& getLoad(void){ return(this->data_to_load); }

public:
	U32 selected_chunk;
	U32 desired_chunks;
	U64 n_comparisons_chunk;

	std::vector<value_type> blocks;
	std::vector< std::pair<U32, U32> > data_to_load;
	std::vector< std::vector<LoadBalancerThread> > thread_distribution;
};

}
#endif /* ALGORITHM_LOAD_BALANCER_LD_H_ */
