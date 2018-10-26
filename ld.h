#ifndef TWK_LD_H_
#define TWK_LD_H_

#include <cassert>
#include <thread>

#include "core.h"
#include "twk_reader.h"
#include "fisher_math.h"
#include "spinlock.h"
#include "timer.h"
#include "writer.h"

// Method 0: None
// Method 1: Performance + no math
// Method 2: Debug correctness
// Method 3: Print LD difference Phased and Unphased
#define SLAVE_DEBUG_MODE 0

namespace tomahawk {

#define TWK_LD_SIMD_REFREF 0
#define TWK_LD_SIMD_ALTREF 1
#define TWK_LD_SIMD_REFALT 2
#define TWK_LD_SIMD_ALTALT 3

// Parameter thresholds for FLAGs
#define LOW_AC_THRESHOLD        5
#define LONG_RANGE_THRESHOLD    500e3
#define MINIMUM_ALLOWED_ALLELES 5		// Minimum number of alleles required for math to work out in the unphased case
#define ALLOWED_ROUNDING_ERROR  0.001

#define TWK_HAP_FREQ(A,POS) ((double)(A).alleleCounts[POS] / (A).totalHaplotypeCounts)

#if SLAVE_DEBUG_MODE == 3
static uint32_t twk_debug_pos1 = 0;
static uint32_t twk_debug_pos1_2 = 0;
static uint32_t twk_debug_pos2 = 0;
static uint32_t twk_debug_pos2_2 = 0;
#endif

/****************************
*  SIMD definitions
****************************/
#if SIMD_AVAILABLE == 1

#ifdef _popcnt64
#define POPCOUNT_ITER	_popcnt64
#else
#define POPCOUNT_ITER	__builtin_popcountll
#endif

#define UNPHASED_UPPER_MASK	170  // 10101010b
#define UNPHASED_LOWER_MASK	85   // 01010101b
#define FILTER_UNPHASED_BYTE(A, B)            ((((((A) & UNPHASED_UPPER_MASK) | ((B) & UNPHASED_LOWER_MASK)) & UNPHASED_LOWER_MASK) << 1) & (A))
#define FILTER_UNPHASED_BYTE_PAIR(A, B, C, D) ((FILTER_UNPHASED_BYTE((A), (B)) >> 1) | FILTER_UNPHASED_BYTE((C), (D)))
#define FILTER_UNPHASED_BYTE_SPECIAL(A)       ((((A) >> 1) & (A)) & UNPHASED_LOWER_MASK)

#if SIMD_VERSION == 6 // AVX-512: UNTESTED
#define VECTOR_TYPE	__m512i
const VECTOR_TYPE ONE_MASK         = _mm512_set1_epi8(255); // 11111111b
const VECTOR_TYPE maskUnphasedHigh = _mm512_set1_epi8(UNPHASED_UPPER_MASK);	// 10101010b
const VECTOR_TYPE maskUnphasedLow  = _mm512_set1_epi8(UNPHASED_LOWER_MASK);	// 01010101b

#define PHASED_ALTALT(A,B)        _mm512_and_si512(A, B)
#define PHASED_REFREF(A,B)        _mm512_and_si512(_mm512_xor_si512(A, ONE_MASK), _mm512_xor_si512(B, ONE_MASK))
#define PHASED_ALTREF(A,B)        _mm512_and_si512(_mm512_xor_si512(A, B), B)
#define PHASED_REFALT(A,B)        _mm512_and_si512(_mm512_xor_si512(A, B), A)
#define PHASED_ALTALT_MASK(A,B,M) _mm512_and_si512(PHASED_ALTALT(A, B), M)
#define PHASED_REFREF_MASK(A,B,M) _mm512_and_si512(PHASED_REFREF(A, B), M)
#define PHASED_ALTREF_MASK(A,B,M) _mm512_and_si512(PHASED_ALTREF(A, B), M)
#define PHASED_REFALT_MASK(A,B,M) _mm512_and_si512(PHASED_REFALT(A, B), M)
#define MASK_MERGE(A,B)           _mm512_xor_si512(_mm512_or_si512(A, B), ONE_MASK)

#define POPCOUNT(A, B) {                                \
	__m256i tempA = _mm512_extracti64x4_epi64(B, 0);    \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 0)); \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 1)); \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 2)); \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 3)); \
	tempA = _mm512_extracti64x4_epi64(B, 1);            \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 0)); \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 1)); \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 2)); \
	A += POPCOUNT_ITER(_mm256_extract_epi64(tempA, 3)); \
}

#define FILTER_UNPHASED(A, B)            _mm512_and_si512(_mm512_slli_epi64(_mm512_and_si512(_mm512_or_si512(_mm512_and_si512(A, maskUnphasedHigh),_mm512_and_si512(B, maskUnphasedLow)), maskUnphasedLow), 1), A)
#define FILTER_UNPHASED_PAIR(A, B, C, D) _mm512_or_si512(_mm512_srli_epi64(FILTER_UNPHASED(A, B), 1), FILTER_UNPHASED(C, D))
#define FILTER_UNPHASED_SPECIAL(A)       _mm512_and_si512(_mm512_and_si512(_mm512_srli_epi64(A, 1), A), maskUnphasedLow)


#elif SIMD_VERSION == 5 // AVX2
#define VECTOR_TYPE	__m256i
const VECTOR_TYPE ONE_MASK         = _mm256_set1_epi8(255); // 11111111b
const VECTOR_TYPE maskUnphasedHigh = _mm256_set1_epi8(UNPHASED_UPPER_MASK);	// 10101010b
const VECTOR_TYPE maskUnphasedLow  = _mm256_set1_epi8(UNPHASED_LOWER_MASK);	// 01010101b

#define PHASED_ALTALT(A,B)        _mm256_and_si256(A, B)
#define PHASED_REFREF(A,B)        _mm256_and_si256(_mm256_xor_si256(A, ONE_MASK), _mm256_xor_si256(B, ONE_MASK))
#define PHASED_ALTREF(A,B)        _mm256_and_si256(_mm256_xor_si256(A, B), B)
#define PHASED_REFALT(A,B)        _mm256_and_si256(_mm256_xor_si256(A, B), A)
#define PHASED_ALTALT_MASK(A,B,M) _mm256_and_si256(PHASED_ALTALT(A, B), M)
#define PHASED_REFREF_MASK(A,B,M) _mm256_and_si256(PHASED_REFREF(A, B), M)
#define PHASED_ALTREF_MASK(A,B,M) _mm256_and_si256(PHASED_ALTREF(A, B), M)
#define PHASED_REFALT_MASK(A,B,M) _mm256_and_si256(PHASED_REFALT(A, B), M)
#define MASK_MERGE(A,B)           _mm256_xor_si256(_mm256_or_si256(A, B), ONE_MASK)

// Software intrinsic popcount
#define POPCOUNT(A, B) {							\
	A += POPCOUNT_ITER(_mm256_extract_epi64(B, 0));	\
	A += POPCOUNT_ITER(_mm256_extract_epi64(B, 1));	\
	A += POPCOUNT_ITER(_mm256_extract_epi64(B, 2));	\
	A += POPCOUNT_ITER(_mm256_extract_epi64(B, 3));	\
}

#define FILTER_UNPHASED(A, B)            _mm256_and_si256(_mm256_slli_epi64(_mm256_and_si256(_mm256_or_si256(_mm256_and_si256(A, maskUnphasedHigh),_mm256_and_si256(B, maskUnphasedLow)), maskUnphasedLow), 1), A)
#define FILTER_UNPHASED_PAIR(A, B, C, D) _mm256_or_si256(_mm256_srli_epi64(FILTER_UNPHASED(A, B), 1), FILTER_UNPHASED(C, D))
#define FILTER_UNPHASED_SPECIAL(A)       _mm256_and_si256(_mm256_and_si256(_mm256_srli_epi64(A, 1), A), maskUnphasedLow)

#elif SIMD_VERSION >= 2 // SSE2+
#define VECTOR_TYPE	__m128i
const VECTOR_TYPE ONE_MASK         = _mm_set1_epi8(255); // 11111111b
const VECTOR_TYPE maskUnphasedHigh = _mm_set1_epi8(UNPHASED_UPPER_MASK);	// 10101010b
const VECTOR_TYPE maskUnphasedLow  = _mm_set1_epi8(UNPHASED_LOWER_MASK);	// 01010101b

#define PHASED_ALTALT(A,B)        _mm_and_si128(A, B)
#define PHASED_REFREF(A,B)        _mm_and_si128(_mm_xor_si128(A, ONE_MASK), _mm_xor_si128(B, ONE_MASK))
#define PHASED_ALTREF(A,B)        _mm_and_si128(_mm_xor_si128(A, B), B)
#define PHASED_REFALT(A,B)        _mm_and_si128(_mm_xor_si128(A, B), A)
#define PHASED_ALTALT_MASK(A,B,M) _mm_and_si128(PHASED_ALTALT(A, B), M)
#define PHASED_REFREF_MASK(A,B,M) _mm_and_si128(PHASED_REFREF(A, B), M)
#define PHASED_ALTREF_MASK(A,B,M) _mm_and_si128(PHASED_ALTREF(A, B), M)
#define PHASED_REFALT_MASK(A,B,M) _mm_and_si128(PHASED_REFALT(A, B), M)
#define MASK_MERGE(A,B)           _mm_xor_si128(_mm_or_si128(A, B), ONE_MASK)

#if SIMD_VERSION >= 3
#define POPCOUNT(A, B) {								\
	A += POPCOUNT_ITER(_mm_extract_epi64(B, 0));		\
	A += POPCOUNT_ITER(_mm_extract_epi64(B, 1));		\
}
#else
#define POPCOUNT(A, B) {      \
	uint64_t temp = _mm_extract_epi16(B, 0) << 6 | _mm_extract_epi16(B, 1) << 4 | _mm_extract_epi16(B, 2) << 2 | _mm_extract_epi16(B, 3); \
	A += POPCOUNT_ITER(temp); \
	temp = _mm_extract_epi16(B, 4) << 6 | _mm_extract_epi16(B, 5) << 4 | _mm_extract_epi16(B, 6) << 2 | _mm_extract_epi16(B, 7); \
	A += POPCOUNT_ITER(temp); \
}
#endif

#define FILTER_UNPHASED(A, B)            _mm_and_si128(_mm_slli_epi64(_mm_and_si128(_mm_or_si128(_mm_and_si128(A, maskUnphasedHigh),_mm_and_si128(B, maskUnphasedLow)), maskUnphasedLow), 1), A)
#define FILTER_UNPHASED_PAIR(A, B, C, D) _mm_or_si128(_mm_srli_epi64(FILTER_UNPHASED(A, B), 1), FILTER_UNPHASED(C, D))
#define FILTER_UNPHASED_SPECIAL(A)       _mm_and_si128(_mm_and_si128(_mm_srli_epi64(A, 1), A), maskUnphasedLow)
#endif
#endif // ENDIF SIMD_AVAILABLE == 1

// Supportive structure for timings
struct twk_ld_perf {
	uint64_t* cycles;
	uint64_t* freq;
};

/**<
 * Work balancer for twk_ld_engine threads. Uses a non-blocking spinlock to produce a
 * tuple (from,to) of integers representing the start ref block and dst block.
 * This approach allows perfect load-balancing at a small overall CPU cost.
 */
struct twk_ld_ticker {
	typedef bool (twk_ld_ticker::*get_func)(uint32_t& from, uint32_t& to, uint8_t& type);

	twk_ld_ticker() : n_perf(0), i(0), j(0), ldd(nullptr), diag(false), window(false), fL(0), tL(0), fR(0), tR(0), l_window(0), _getfunc(&twk_ld_ticker::GetBlockPair){}
	~twk_ld_ticker(){}

	inline void SetWindow(const bool yes = true){
		window = yes;
		_getfunc = (window ? &twk_ld_ticker::GetBlockWindow : &twk_ld_ticker::GetBlockPair);
	}

	inline bool Get(uint32_t& from, uint32_t& to, uint8_t& type){ return((this->*_getfunc)(from, to, type)); }

	bool GetBlockWindow(uint32_t& from, uint32_t& to, uint8_t& type){
		spinlock.lock();

		if(j == tR){
			++i; j = (diag ? i : fR); from = i; to = j; type = 1; ++j;
			if(i == tL){ spinlock.unlock(); return false; }
			++n_perf;
			spinlock.unlock();
			return true;
		}
		if(i == tL){ spinlock.unlock(); return false; }

		// First in tgt block - last in ref block
		if(i != j){
			if(ldd[j].blk->rcds[0].pos - ldd[i].blk->rcds[ldd[i].n_rec-1].pos > l_window){
				//std::cerr << "never overlap=" << ldd[j].blk->rcds[0].pos << "-" << ldd[i].blk->rcds[ldd[i].n_rec - 1].pos << "=" << ldd[j].blk->rcds[0].pos - ldd[i].blk->rcds[ldd[i].n_rec - 1].pos << std::endl;
				//std::cerr << i << "," << j << "," << (int)type << std::endl;
				++i; j = i; type = 1;
				spinlock.unlock();
				return true;
			}
		}

		type = (i == j); from = i; to = j;
		++j; ++n_perf;

		spinlock.unlock();

		return true;
	}

	bool GetBlockPair(uint32_t& from, uint32_t& to, uint8_t& type){
		spinlock.lock();

		if(j == tR){
			++i; j = (diag ? i : fR); from = i; to = j; type = 1; ++j;
			if(i == tL){ spinlock.unlock(); return false; }
			++n_perf;
			spinlock.unlock();
			return true;
		}
		if(i == tL){ spinlock.unlock(); return false; }
		type = (i == j); from = i; to = j;
		++j; ++n_perf;

		spinlock.unlock();

		return true;
	}

	uint32_t n_perf;
	SpinLock spinlock;
	uint32_t i,j;
	twk1_ldd_blk* ldd;

	bool diag, window;
	uint32_t fL, tL, fR, tR, l_window;
	get_func _getfunc;
};

/**<
 * Progress ticker for calculating linkage-disequilbirium. Spawns and detaches
 * a thread to tick occasionally in the background. Slaves computing linkage-
 * disequilibrium send their progress to this ticker that collates and summarize
 * that data.
 */
struct twk_ld_progress {
	twk_ld_progress() : is_ticking(false), n_s(0), n_var(0), n_pair(0), n_out(0), b_out(0), thread(nullptr){}
	~twk_ld_progress() = default;

	std::thread* Start(){
		delete thread;
		is_ticking = true;
		thread = new std::thread(&twk_ld_progress::StartTicking, this);
		thread->detach();
		return(thread);
	}

	void StartTicking(){
		uint32_t i = 0;
		timer.Start();

		uint64_t variant_overflow = 99E9, genotype_overflow = 999E12;
		uint8_t variant_width = 15, genotype_width = 20;

		//char support_buffer[256];
		std::cerr << utility::timestamp("PROGRESS")
				<< std::setw(12) << "Time elapsed"
				<< std::setw(variant_width) << "Variants"
				<< std::setw(genotype_width) << "Genotypes"
				<< std::setw(15) << "Output"
				<< std::setw(10) << "Progress"
				<< "\tEst. Time left" << std::endl;

		while(is_ticking){
			// Triggered every cycle (119 ms)
			//if(this->Detailed)
			//	this->GetElapsedTime();

			++i;
			if(i % 252 == 0){ // Approximately every 30 sec (30e3 / 119 = 252)
				//const double ComparisonsPerSecond = (double)n_var.load()/timer.Elapsed().count();
				if(n_var.load() > variant_overflow){ variant_width += 3; variant_overflow *= 1e3; }
				if(n_var.load() > genotype_overflow){ genotype_width += 3; genotype_overflow *= 1e3; }


				//const uint32_t n_p = sprintf(&support_buffer[0], "%0.3f", 0);
				//support_buffer[n_p] = '%';
				std::cerr << utility::timestamp("PROGRESS")
						<< std::setw(12) << timer.ElapsedString()
						<< std::setw(variant_width) << utility::ToPrettyString(n_var.load())
						<< std::setw(genotype_width) << utility::ToPrettyString(n_var.load()*n_s)
						<< std::setw(15) << utility::ToPrettyString(n_out.load())
						<< std::setw(10) << 0 << '\t'
						<< 0 << std::endl;
				i = 0;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(119));
		}
		is_ticking = false;

		// Final output
		//if(this->Detailed)
		//	this->GetElapsedTime();
	}

	/**<
	 * Print out the final tally of time elapsed, number of variants computed,
	 * and average throughput. This method cannot be made const as the function
	 * ElapsedString in the Timer class internally updates a buffer for performance
	 * reasons. This has no consequence as this function is ever only called once.
	 */
	void PrintFinal(){
		std::cerr << utility::timestamp("PROGRESS") << this->timer.ElapsedString() << "\t" << utility::ToPrettyString(n_var.load()) << "\t" << utility::ToPrettyString(n_var.load()*n_s) << "\t" << utility::ToPrettyString(n_out.load()) << std::endl;
		std::cerr << utility::timestamp("PROGRESS") << utility::ToPrettyString((uint64_t)((double)n_var.load()/timer.Elapsed().count())) << "\t" << utility::ToPrettyString((uint64_t)(((double)n_var.load()*n_s)/timer.Elapsed().count())) << std::endl;
		std::cerr << utility::timestamp("PROGRESS") << "Finished" << std::endl;
	}

	bool is_ticking;
	uint32_t n_s;
	std::atomic<uint64_t> n_var, n_pair, n_out, b_out;
	std::thread* thread;
	Timer timer;
};


/**<
 * Supportive counter struct for when using the vectorized instructions
 * for computing linkage-disequilibrium.
 */
struct twk_ld_simd {
public:
	twk_ld_simd(void);
	~twk_ld_simd();

public:
	uint64_t *counters;
	uint8_t  *scalarA, *scalarB, *scalarC, *scalarD;
} __attribute__((aligned(16)));

/**<
 * Generic supportive counter struct for when calculating linkage-disequilibrium.
 * This struct have internal counters for haplotype and allele counts and for the
 * total number of allele/haplotype counts.
 */
struct twk_ld_count {
	twk_ld_count(): totalHaplotypeCounts(0)
	{
		// Initialize counters to 0. This is generally not necessary as each
		// function computing LD clears these. However, it is good practice.
		memset(alleleCounts, 171, sizeof(uint64_t)*171);
		memset(haplotypeCounts, 4, sizeof(uint64_t)*4);
	}
	~twk_ld_count(){}

	void resetPhased(void){
		this->alleleCounts[0]  = 0;
		this->alleleCounts[1]  = 0;
		this->alleleCounts[4]  = 0;
		this->alleleCounts[5]  = 0;
		haplotypeCounts[0] = 0;
		haplotypeCounts[1] = 0;
		haplotypeCounts[2] = 0;
		haplotypeCounts[3] = 0;
		// All other values can legally overflow
		// They are not used
	}

	void resetUnphased(void){
		this->alleleCounts[0]  = 0;
		this->alleleCounts[1]  = 0;
		this->alleleCounts[4]  = 0;
		this->alleleCounts[5]  = 0;
		this->alleleCounts[16] = 0;
		this->alleleCounts[17] = 0;
		this->alleleCounts[20] = 0;
		this->alleleCounts[21] = 0;
		this->alleleCounts[64] = 0;
		this->alleleCounts[65] = 0;
		this->alleleCounts[68] = 0;
		this->alleleCounts[69] = 0;
		this->alleleCounts[80] = 0;
		this->alleleCounts[81] = 0;
		this->alleleCounts[84] = 0;
		this->alleleCounts[85] = 0;
		// All other values can legally overflow
		// They are not used
	}

	// Counters
	uint64_t alleleCounts[171]; // equivalent to 10101010b = (MISS,MISS),(MISS,MISS)
	uint64_t haplotypeCounts[4]; // haplotype counts
	uint64_t totalHaplotypeCounts; // Total number of alleles
};

/**<
 * Load balancer for calculating linkage-disequilibrium. Partitions the total
 * problem into psuedo-balanced subproblems. The size and number of sub-problems
 * can be parameterized.
 */
struct twk_ld_balancer {
	twk_ld_balancer() : diag(false), n(0), p(0), c(0), fromL(0), toL(0), fromR(0), toR(0), n_m(0){}

	/**<
	 * Find the desired target subproblem range as a tuple (fromL,toL,fromR,toR).
	 * @param n_blocks      Total number of blocks.
	 * @param desired_parts Desired number of subproblems to solve.
	 * @param chosen_part   Target subproblem we are interested in getting the ranges for.
	 * @return              Return TRUE upon success or FALSE otherwise.
	 */
	bool Build(uint32_t n_blocks,
	           uint32_t desired_parts,
	           uint32_t chosen_part)
	{
		if(chosen_part >= desired_parts){
			std::cerr << "illegal chosen block" << std::endl;
			return false;
		}
		n = n_blocks; p = desired_parts; c = chosen_part;
		if(p > n) p = n;

		if(p == 1){
			p = 1; c = 0;
			fromL = 0; toL = n_blocks; fromR = 0; toR = n_blocks;
			n_m = n_blocks; diag = true;
			return true;
		}

		uint32_t factor = 0;
		for(int i = 1; i < desired_parts; ++i){
			if( (((i*i) - i) / 2) + i == desired_parts ){
				std::cerr << "factor is " << i << std::endl;
				factor = i;
				break;
			}
		}

		if(factor == 0){
			std::cerr << "could not partition into subproblem=" << desired_parts << std::endl;
			return false;
		}

		// cycle
		uint32_t chunk_size = n / factor;
		uint32_t fL = 0, tL = 0, fR = 0, tR = 0;
		for(int i = 0, k = 0; i < factor; ++i){ // rows
			for(int j = i; j < factor; ++j, ++k){ // cols
				tR = (j + 1 == factor ? n_blocks : chunk_size*(j+1));
				fR = tR - chunk_size;
				tL = (i + 1 == factor ? n_blocks : chunk_size*(i+1));
				fL = tL - chunk_size;
				//std::cerr << fL << "-" << tL << "->" << fR << "-" << tR << " total=" << n_blocks << " chunk=" << chunk_size << "desired=" << desired_parts << std::endl;
				if(k == chosen_part){
					//std::cerr << "chosen part" << std::endl;
					fromL = fL; toL = tL; fromR = fR; toR = tR;
					// Square
					//std::cerr << "from=" << toL - fromL << "," << toR - fromR << std::endl;
					n_m = (toL - fromL) + (toR - fromR); diag = false;
					if(i == j){ n_m = toL - fromL; diag = true; }
					break;
				}
			}
		}
		return true;
	}

public:
	bool diag; // is selectd chunk diagonal
	uint32_t n, p, c; // number of available blocks, desired parts, chosen part
	uint32_t fromL, toL, fromR, toR;
	uint32_t n_m; // actual blocks used
};

/**<
 * Settings/parameters for both `twk_ld` and `twk_ld_engine`.
 */
struct twk_ld_settings {
	twk_ld_settings() :
		square(true), window(false),
		c_level(1), b_size(10000), l_window(1000000),
		minP(1e-2), minR2(0.1), minDprime(0),
		n_chunks(0), c_chunk(0)
	{}

	std::string GetString() const{
		std::string s =  "square=" + std::string((square ? "TRUE" : "FALSE"))
		              + ",window=" + std::string((window ? "TRUE" : "FALSE"))
					  + ",compression_level=" + std::to_string(c_level)
		              + ",block_size=" + std::to_string(b_size)
		              + (window ? std::string(",window_size=") + std::to_string(l_window) : "")
					  + ",minP=" + std::to_string(minP)
					  + ",minR2=" + std::to_string(minR2)
		 	 	 	  + ",minDprime=" + std::to_string(minDprime)
					  + ",n_chunks=" + std::to_string(c_chunk);
		return(s);
	}

	bool square, window; // using square compute, using window compute
	int32_t c_level, b_size, l_window; // compression level, block size, window size in bp
	std::string in, out; // input file, output file/cout
	double minP, minR2, minDprime;
	int32_t n_chunks, c_chunk;
};

/**<
 * Internal engine for computing linkage-disequilibrium. Invocation of these
 * functions are generally performed outside of this class in the spawned
 * slaves. This separation of invokation and compute allows for embarassingly
 * parallel compute of non-overlapping blocks without blocking resources.
 */
class twk_ld_engine {
public:
	// Function definitions to computing functions defined below.
	typedef bool (twk_ld_engine::*func)(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf);
	typedef bool (twk_ld_engine::*ep[8])(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf);

public:
	twk_ld_engine() :
		n_samples(0), n_out(0), n_lim(10000), n_out_tick(250),
		byte_width(0), byte_aligned_end(0), vector_cycles(0),
		phased_unbalanced_adjustment(0), unphased_unbalanced_adjustment(0), t_out(0),
		index(nullptr), writer(nullptr), progress(nullptr)
	{}

	/**<
	 * Set the number of samples in the target file. This function is mandatory
	 * to call prior to computing as it sets essential paramters such as the number
	 * of samples, the byte alignment of vectorized bit-vectors and the adjustment
	 * given the unused overhangs.
	 * @param samples Number of samples in the target file.
	 */
	void SetSamples(const uint32_t samples){
		n_samples  = samples;
		byte_width = ceil((double)samples/4);
		byte_aligned_end = byte_width/(GENOTYPE_TRIP_COUNT/4)*(GENOTYPE_TRIP_COUNT/4);
		vector_cycles    = byte_aligned_end*4/GENOTYPE_TRIP_COUNT;
		phased_unbalanced_adjustment   = (samples*2)%8;
		unphased_unbalanced_adjustment = samples%4;
	}

	void SetBlocksize(const uint32_t s){
		assert(s % 2 == 0);
		blk_f.clear(); blk_r.clear();
		blk_f.resize(s + 100);
		blk_r.resize(s + 100);
		obuf.resize(s * sizeof(twk1_two_t));
		ibuf.resize(s * sizeof(twk1_two_t));

		n_out = 0; n_lim = s;
	}

	/**<
	 * Phased/unphased functions for calculating linkage-disequilibrium. These
	 * functions are all prefixed with Phased_ or Unphased_. All these functions
	 * share the same interface in order to allow them being targetted by a shared
	 * function pointer.
	 * @param b1   Left twk1_ldd_blk reference.
	 * @param p1   Left relative offset into the left twk1_ldd_blk reference.
	 * @param b2   Right twk_1_ldd_blk reference.
	 * @param p2   Right relative offset into the right twk1_ldd_blk reference.
	 * @param perf Pointer to a twk_ld_perf object if performance measure are to be taken. This value can be set to nullptr.
	 * @return     Returns TRUE upon success or FALSE otherwise.
	 */
	bool PhasedRunlength(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr);
	bool PhasedList(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr);
	bool PhasedVectorized(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr);
	bool PhasedVectorizedNoMissing(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr);
	bool PhasedMath(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2);
	bool UnphasedRunlength(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr);
	bool UnphasedVectorized(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr);
	bool UnphasedVectorizedNoMissing(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr);

	// Specialized hybrid functions
	__attribute__((always_inline))
	inline bool HybridUnphased(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr){
		if(b1.blk->rcds[p1].gt->n + b2.blk->rcds[p2].gt->n < 40)
			return(UnphasedRunlength(b1,p1,b2,p2,perf));
		else return(UnphasedVectorizedNoMissing(b1,p1,b2,p2,perf));
	}

	// Debugging function for invoking each algorithm on the same data.
	inline bool AllAlgorithms(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2, twk_ld_perf* perf = nullptr){
		//this->PhasedVectorized(b1,p1,b2,p2,perf);
		this->PhasedVectorizedNoMissing(b1,p1,b2,p2,perf);
		//this->PhasedVectorizedNoMissingNoTable(b1,p1,b2,p2,perf);
		//this->UnphasedVectorized(b1,p1,b2,p2,perf);
		this->UnphasedVectorizedNoMissing(b1,p1,b2,p2,perf);
		//this->PhasedRunlength(b1,p1,b2,p2,perf);
		//this->PhasedList(b1,p1,b2,p2,perf);
		//this->UnphasedRunlength(b1,p1,b2,p2,perf);
		return true;
	}

	// Unphased math
	bool UnphasedMath(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2);
	double ChiSquaredUnphasedTable(const double& target, const double& p, const double& q);
	bool ChooseF11Calculate(const twk1_ldd_blk& b1, const uint32_t& p1, const twk1_ldd_blk& b2, const uint32_t& p2,const double& target, const double& p, const double& q);

	/**<
	 * Compress the internal output buffers consisting of twk1_two_t records.
	 * @return Returns TRUE upon success or FALSE otherwise.
	 */
	bool CompressBlock();
	bool CompressFwd();
	bool CompressRev();

public:
	uint32_t n_samples;
	uint32_t n_out, n_lim, n_out_tick;
	uint32_t byte_width; // Number of bytes required per variant site
	uint32_t byte_aligned_end; // End byte position
	uint32_t vector_cycles; // Number of SIMD cycles (genotypes/2/vector width)
	uint32_t phased_unbalanced_adjustment; // Modulus remainder
	uint32_t unphased_unbalanced_adjustment; // Modulus remainder
	uint64_t t_out; // number of bytes written

	IndexEntryOutput irecF, irecR;
	ZSTDCodec zcodec; // reusable zstd codec instance with internal context.

	twk_ld_settings settings;
	twk1_two_block_t blk_f, blk_r;
	twk_buffer_t ibuf, obuf; // buffers
	twk_ld_simd  helper_simd; // simd counters
	twk_ld_count helper; // regular counters
	twk1_two_t cur_rcd;
	twk1_two_t cur_rcd2;
	IndexOutput* index;
	twk_writer_t* writer;
	twk_ld_progress* progress;
};

struct twk_ld_slave {
	twk_ld_slave() : n_s(0), n_total(0), ticker(nullptr), thread(nullptr), ldd(nullptr), progress(nullptr){}
	~twk_ld_slave(){ delete thread; }

	std::thread* Start(){
		delete thread;
		thread = new std::thread(&twk_ld_slave::CalculatePhased, this);
		return(thread);
	}

	bool CalculateGeneral(){

	}

	bool CalculatePhased(){
		twk1_ldd_blk blocks[2];
		uint32_t from, to; uint8_t type;
		Timer timer; timer.Start();

		const uint32_t i_start = ticker->fL;
		const uint32_t j_start = ticker->fR;

		while(true){
			if(!ticker->Get(from, to, type)) break;

			blocks[0].SetPreloaded(ldd[from - i_start]);
			blocks[1].SetPreloaded(ldd[to - j_start]);

			if(type == 1){
				for(int i = 0; i < blocks[0].n_rec; ++i){
					for(int j = i+1; j < blocks[0].n_rec; ++j){

						if(blocks[0].blk->rcds[i].ac + blocks[0].blk->rcds[j].ac < 5){
							continue;
						}

						if(std::min(blocks[0].blk->rcds[i].ac,blocks[0].blk->rcds[j].ac) < 50)
						//engine.PhasedVectorized(blocks[0],i,blocks[0],j,nullptr);
						//engine.PhasedVectorizedNoMissingNoTable(blocks[0],i,blocks[0],j,nullptr);
						engine.PhasedList(blocks[0],i,blocks[0],j,nullptr);
						else {
							engine.PhasedVectorized(blocks[0],i,blocks[0],j,nullptr);
						}
					}
				}
				progress->n_var += ((blocks[0].n_rec * blocks[0].n_rec) - blocks[0].n_rec) / 2; // n choose 2
			} else {
				for(int i = 0; i < blocks[0].n_rec; ++i){
					for(int j = 0; j < blocks[1].n_rec; ++j){
						if( blocks[0].blk->rcds[i].ac + blocks[1].blk->rcds[j].ac < 5 ){
							continue;
						}

						if(std::min(blocks[0].blk->rcds[i].ac,blocks[1].blk->rcds[j].ac) < 50)
						//	engine.PhasedVectorized(blocks[0],i,blocks[1],j,nullptr);
						//	engine.PhasedVectorizedNoMissingNoTable(blocks[0],i,blocks[1],j,nullptr);
							engine.PhasedList(blocks[0],i,blocks[1],j,nullptr);
						else {
							engine.PhasedVectorized(blocks[0],i,blocks[1],j,nullptr);
						}
					}
				}
				progress->n_var += blocks[0].n_rec * blocks[1].n_rec;
			}
		}
		//std::cerr << "done" << std::endl;

		// if preloaded
		blocks[0].vec = nullptr; blocks[0].list = nullptr;
		blocks[1].vec = nullptr; blocks[1].list = nullptr;

		return true;
	}

	bool ComputeUnphased(){
		twk1_ldd_blk blocks[2];
		uint32_t from, to; uint8_t type;
		Timer timer; timer.Start();

		const uint32_t i_start = ticker->fL;
		const uint32_t j_start = ticker->fR;

		while(true){
			if(!ticker->Get(from, to, type)) break;

			blocks[0].SetPreloaded(ldd[from - i_start]);
			blocks[1].SetPreloaded(ldd[to - j_start]);

			if(type == 1){
				for(int i = 0; i < blocks[0].n_rec; ++i){
					for(int j = i+1; j < blocks[0].n_rec; ++j){

						if(blocks[0].blk->rcds[i].ac + blocks[0].blk->rcds[j].ac < 5){
							continue;
						}

						if(blocks[0].blk->rcds[i].ac + blocks[0].blk->rcds[j].ac < 50)
						//engine.PhasedVectorized(blocks[0],i,blocks[0],j,nullptr);
						//engine.PhasedVectorizedNoMissingNoTable(blocks[0],i,blocks[0],j,nullptr);
						engine.UnphasedRunlength(blocks[0],i,blocks[0],j,nullptr);
						else {
							engine.UnphasedVectorized(blocks[0],i,blocks[0],j,nullptr);
						}
					}
				}
				progress->n_var += ((blocks[0].n_rec * blocks[0].n_rec) - blocks[0].n_rec) / 2; // n choose 2
			} else {
				for(int i = 0; i < blocks[0].n_rec; ++i){
					for(int j = 0; j < blocks[1].n_rec; ++j){
						if( blocks[0].blk->rcds[i].ac + blocks[1].blk->rcds[j].ac < 5 ){
							continue;
						}

						if(blocks[0].blk->rcds[i].ac + blocks[1].blk->rcds[j].ac < 50)
						//	engine.PhasedVectorized(blocks[0],i,blocks[1],j,nullptr);
						//	engine.PhasedVectorizedNoMissingNoTable(blocks[0],i,blocks[1],j,nullptr);
							engine.UnphasedRunlength(blocks[0],i,blocks[1],j,nullptr);
						else {
							engine.UnphasedVectorized(blocks[0],i,blocks[1],j,nullptr);
						}
					}
				}
				progress->n_var += blocks[0].n_rec * blocks[1].n_rec;
			}
		}
		//std::cerr << "done" << std::endl;

		// if preloaded
		blocks[0].vec = nullptr; blocks[0].list = nullptr;
		blocks[1].vec = nullptr; blocks[1].list = nullptr;

		return true;
	}

public:
	uint32_t n_s;
	uint64_t n_total;
	twk_ld_ticker* ticker;
	std::thread* thread;
	twk1_ldd_blk* ldd;
	twk_ld_progress* progress;
	twk_ld_engine engine;
};

class twk_ld {
public:

	void operator=(const twk_ld_settings& settings){ this->settings = settings; }

	bool Compute(const twk_ld_settings& settings){
		this->settings = settings;
		return(Compute());
	}

	bool Compute(){
		ProgramMessage();
		std::cerr << utility::timestamp("LOG") << "Calling calc..." << std::endl;
		if(settings.in.size() == 0){
			std::cerr << "no filename" << std::endl;
			return false;
		}
		std::cerr << utility::timestamp("LOG","READER") << "Opening " << settings.in << "..." << std::endl;

		twk_reader reader;
		if(reader.Open(settings.in) == false){
			std::cerr << "failed" << std::endl;
			return false;
		}

		twk_writer_t* writer = nullptr;
		if(settings.out.size() == 0 || (settings.out.size() == 1 && settings.out[0] == '-')){
			std::cerr << utility::timestamp("LOG","WRITER") << "Writing to " << "stdout..." << std::endl;
			writer = new twk_writer_stream;
		} else {
			std::cerr << utility::timestamp("LOG","WRITER") << "Opening " << settings.out << "..." << std::endl;

			writer = new twk_writer_file;
			if(writer->Open(settings.out) == false){
				std::cerr << "failed to open" << std::endl;
				delete writer;
				return false;
			}
		}
		std::cerr << utility::timestamp("LOG") << "Samples: " << utility::ToPrettyString(reader.hdr.GetNumberSamples()) << " and blocks: " << utility::ToPrettyString(reader.index.n) << "..." << std::endl;

		tomahawk::ZSTDCodec zcodec;
		writer->write(tomahawk::TOMAHAWK_LD_MAGIC_HEADER.data(), tomahawk::TOMAHAWK_LD_MAGIC_HEADER_LENGTH);
		tomahawk::twk_buffer_t buf(256000), obuf(256000);
		buf << reader.hdr;
		if(zcodec.Compress(buf, obuf, settings.c_level) == false){
			std::cerr << "failed to compress" << std::endl;
			return false;
		}

		writer->write(reinterpret_cast<const char*>(&buf.size()),sizeof(uint64_t));
		writer->write(reinterpret_cast<const char*>(&obuf.size()),sizeof(uint64_t));
		writer->write(obuf.data(),obuf.size());
		writer->stream.flush();
		buf.reset();

		// New index
		IndexOutput index(reader.hdr.GetNumberContigs());

		twk1_blk_iterator bit;
		bit.stream = reader.stream;

		if(settings.window && settings.n_chunks != 1){
			std::cerr << "cannot use chunking in window mode" << std::endl;
			return false;
		}
		if(settings.window) settings.c_chunk = 0;

		twk_ld_balancer balancer;
		if(balancer.Build(reader.index.n, settings.n_chunks, settings.c_chunk) == false){
			std::cerr << "failed balancing" << std::endl;
			return false;
		}

		Timer timer; timer.Start();
		uint32_t n_blocks = balancer.n_m;
		twk1_ldd_blk* ldd = new twk1_ldd_blk[n_blocks];
		uint32_t n_variants = 0;
		if(balancer.diag){
			for(int i = balancer.fromL; i < balancer.toL; ++i) n_variants += reader.index.ent[i].n;
		} else {
			for(int i = balancer.fromL; i < balancer.toL; ++i) n_variants += reader.index.ent[i].n;
			for(int i = balancer.fromR; i < balancer.toR; ++i) n_variants += reader.index.ent[i].n;
		}
		std::cerr << utility::timestamp("LOG","BALANCING") << "Using ranges [" << balancer.fromL << "-" << balancer.toL << "," << balancer.fromR << "-" << balancer.toR << "] in " << (settings.window ? "window mode" : "square mode") <<"..." << std::endl;
		std::cerr << utility::timestamp("LOG") << utility::ToPrettyString(n_variants) << " variants from " << utility::ToPrettyString(n_blocks) << " blocks..." << std::endl;
		bool pre_build = true;

		std::cerr << utility::timestamp("LOG","PARAMS") << settings.GetString() << std::endl;


#if SIMD_AVAILABLE == 1
		std::cerr << utility::timestamp("LOG","SIMD") << "Vectorized instructions available: " << TWK_SIMD_MAPPING[SIMD_VERSION] << "..." << std::endl;
#else
		std::cerr << utility::timestamp("LOG","SIMD") << "No vectorized instructions available..." << std::endl;
#endif
		std::cerr << utility::timestamp("LOG") << "Constructing list, vector, RLE... ";

		timer.Start();
		uint32_t local_block = 0;
		if(pre_build){
			bit.stream->seekg(reader.index.ent[balancer.fromL].foff);
			for(int i = balancer.fromL; i < balancer.toL; ++i){
				if(bit.NextBlockRaw() == false){
					std::cerr << "failed to get->" << i << std::endl;
					return false;
				}

				//std::cerr << "next-block=" << bit.stream->tellg() << " n=" << bit.blk.n << std::endl;
				ldd[local_block].SetOwn(bit, reader.hdr.GetNumberSamples());
				ldd[local_block].Inflate(reader.hdr.GetNumberSamples(),TWK_LDD_ALL, true);
				++local_block;
			}
			if(balancer.diag == false){
				bit.stream->seekg(reader.index.ent[balancer.fromR].foff);
				for(int i = balancer.fromR; i < balancer.toR; ++i){
					if(bit.NextBlockRaw() == false){
						std::cerr << "failed to get->" << i << std::endl;
						return false;
					}

					//std::cerr << "next-block=" << bit.stream->tellg() << " n=" << bit.blk.n << std::endl;
					ldd[local_block].SetOwn(bit, reader.hdr.GetNumberSamples());
					ldd[local_block].Inflate(reader.hdr.GetNumberSamples(),TWK_LDD_ALL, true);
					++local_block;
				}
			}

		} else {
			bit.stream->seekg(reader.index.ent[balancer.fromL].foff);
			for(int i = 0; i < n_blocks; ++i){
				if(bit.NextBlockRaw() == false){
					std::cerr << "failed to get->" << i << std::endl;
					return false;
				}

				//std::cerr << "next-block=" << bit.stream->tellg() << " n=" << bit.blk.n << std::endl;
				ldd[local_block].SetOwn(bit, reader.hdr.GetNumberSamples());
				++local_block;
			}

			if(balancer.diag == false){
				bit.stream->seekg(reader.index.ent[balancer.fromR].foff);
				for(int i = balancer.fromR; i < balancer.toR; ++i){
					if(bit.NextBlockRaw() == false){
						std::cerr << "failed to get->" << i << std::endl;
						return false;
					}

					//std::cerr << "next-block=" << bit.stream->tellg() << " n=" << bit.blk.n << std::endl;
					ldd[local_block].SetOwn(bit, reader.hdr.GetNumberSamples());
					++local_block;
				}
			}
		}
		std::cerr << "Done! " << timer.ElapsedString() << std::endl;
		uint64_t n_vnt_cmps = ((uint64_t)n_variants * n_variants - n_variants) / 2;
		std::cerr << utility::timestamp("LOG") << "Performing: " << utility::ToPrettyString(n_vnt_cmps) << " variant comparisons..." << std::endl;


		twk_ld_ticker ticker;
		ticker.diag = balancer.diag;
		ticker.fL   = balancer.fromL;
		ticker.tL   = balancer.toL;
		ticker.fR   = balancer.fromR;
		ticker.tR   = balancer.toR;
		ticker.ldd  = ldd;
		ticker.i    = balancer.fromL;
		ticker.j    = balancer.fromR;
		ticker.window   = settings.window;
		ticker.l_window = settings.l_window;
		twk_ld_progress progress;
		progress.n_s = reader.hdr.GetNumberSamples();
		uint32_t n_threads = std::thread::hardware_concurrency();
		//n_threads  = 1;
		twk_ld_slave* slaves = new twk_ld_slave[n_threads];
		std::vector<std::thread*> threads(n_threads);

		std::cerr << utility::timestamp("LOG","THREAD") << "Spawning " << n_threads << " threads: ";
		for(int i = 0; i < n_threads; ++i){
			slaves[i].ldd    = ldd;
			slaves[i].n_s    = reader.hdr.GetNumberSamples();
			slaves[i].ticker = &ticker;
			slaves[i].engine.SetSamples(reader.hdr.GetNumberSamples());
			slaves[i].engine.SetBlocksize(settings.b_size);
			slaves[i].engine.progress = &progress;
			slaves[i].engine.writer   = writer;
			slaves[i].engine.index    = &index;
			slaves[i].engine.settings = settings;
			slaves[i].progress = &progress;
			threads[i] = slaves[i].Start();
			std::cerr << ".";
		}
		std::cerr << std::endl;

		progress.Start();

		for(int i = 0; i < n_threads; ++i) threads[i]->join();
		for(int i = 0; i < n_threads; ++i) slaves[i].engine.CompressBlock();
		progress.is_ticking = false;
		progress.PrintFinal();
		writer->stream.flush();

		std::cerr << "performed=" << ticker.n_perf << std::endl;

		buf.reset(); obuf.reset();
		buf << index;
		//std::cerr << "index buf size =" << buf.size() << std::endl;
		if(zcodec.Compress(buf, obuf, settings.c_level) == false){
			std::cerr << "failed compression" << std::endl;
			return false;
		}
		//std::cerr << buf.size() << "->" << obuf.size() << " -> " << (float)buf.size()/obuf.size() << std::endl;

		// temp write out index
		for(int i = 0; i < index.n; ++i){
			std::cerr << i << "/" << index.n << " " << index.ent[i].rid << ":" << index.ent[i].minpos << "-" << index.ent[i].maxpos << " offset=" << index.ent[i].foff << "-" << index.ent[i].fend << " brid=" << index.ent[i].ridB << std::endl;
		}

		const uint64_t offset_start_index = writer->stream.tellp();
		uint8_t marker = 0;
		writer->write(reinterpret_cast<const char*>(&marker),sizeof(uint8_t));
		writer->write(reinterpret_cast<const char*>(&buf.size()),sizeof(uint64_t));
		writer->write(reinterpret_cast<const char*>(&obuf.size()),sizeof(uint64_t));
		writer->write(obuf.data(),obuf.size());
		writer->write(reinterpret_cast<const char*>(&offset_start_index),sizeof(uint64_t));
		writer->write(tomahawk::TOMAHAWK_FILE_EOF.data(), tomahawk::TOMAHAWK_FILE_EOF_LENGTH);
		writer->stream.flush();

		delete[] ldd; delete[] slaves; delete writer;
		return true;

		twk1_ldd_blk blocks[2];

		twk_ld_engine::ep f;

		f[0] = &twk_ld_engine::PhasedVectorized;
		f[1] = &twk_ld_engine::PhasedVectorizedNoMissing;
		f[2] = &twk_ld_engine::UnphasedVectorized;
		f[3] = &twk_ld_engine::UnphasedVectorizedNoMissing;
		f[4] = &twk_ld_engine::PhasedRunlength;
		f[5] = &twk_ld_engine::PhasedList;
		f[6] = &twk_ld_engine::UnphasedRunlength;
		f[7] = &twk_ld_engine::HybridUnphased;

		f[0] = &twk_ld_engine::PhasedList;

		uint8_t unpack_list[10];
		memset(unpack_list, TWK_LDD_VEC, 10);
		unpack_list[5] = TWK_LDD_NONE;
		unpack_list[6] = TWK_LDD_LIST;
		unpack_list[7] = TWK_LDD_NONE;
		unpack_list[8] = TWK_LDD_ALL;
		unpack_list[9] = TWK_LDD_ALL;

		unpack_list[0] = TWK_LDD_LIST;

		twk_ld_perf perfs[10];
		uint32_t perf_size = reader.hdr.GetNumberSamples()*4 + 2;
		for(int i = 0; i < 10; ++i){
			perfs[i].cycles = new uint64_t[perf_size];
			perfs[i].freq   = new uint64_t[perf_size];
			memset(perfs[i].cycles, 0, perf_size*sizeof(uint64_t));
			memset(perfs[i].freq, 0, perf_size*sizeof(uint64_t));
		}

		twk_ld_engine engine;
		engine.SetSamples(reader.hdr.GetNumberSamples());
		engine.SetBlocksize(10000);

		uint32_t dist = 1000000;

		uint32_t from, to; uint8_t type;
		uint64_t n_total = 0;
		uint32_t fl_lim = 0;

		while(true){
			if(!ticker.GetBlockPair(from, to, type)) break;
			if(from == to) std::cerr << utility::timestamp("DEBUG") << from << "," << to << "," << (int)type << std::endl;

			blocks[0] = ldd[from];
			blocks[0].Inflate(reader.hdr.GetNumberSamples(), unpack_list[0]);
			blocks[1] = ldd[to];
			blocks[1].Inflate(reader.hdr.GetNumberSamples(), unpack_list[0]);

			if(type == 1){
				for(int i = 0; i < ldd[from].n_rec; ++i){
					for(int j = i+1; j < ldd[to].n_rec; ++j){
						if(blocks[0].blk->rcds[i].ac + blocks[0].blk->rcds[j].ac < 5){
							continue;
						}

						/*
						engine.PhasedVectorized(blocks[0], i, blocks[1], j);
						engine.PhasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
						engine.PhasedVectorizedNoMissingNoTable(blocks[0], i, blocks[1], j);
						engine.UnphasedVectorized(blocks[0], i, blocks[1], j);
						engine.UnphasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
						engine.Runlength(blocks[0], i, blocks[1], j);
						*/
						//engine.AllAlgorithms(blocks[0],i,blocks[0],j,&perfs[method]);
						//(engine.*f[0])(blocks[0], i, blocks[0], j, &perfs[0]);
						engine.PhasedList(blocks[0],i,blocks[0],j,nullptr);
					}
				}
				n_total += ((blocks[0].n_rec * blocks[0].n_rec) - blocks[0].n_rec) / 2; // n choose 2
			} else {
				for(int i = 0; i < blocks[0].n_rec; ++i){
					for(int j = 0; j < blocks[1].n_rec; ++j){
						if( blocks[0].blk->rcds[i].ac + blocks[1].blk->rcds[j].ac < 5 ){
							continue;
						}

						/*
						engine.PhasedVectorized(blocks[0], i, blocks[1], j);
						engine.PhasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
						engine.PhasedVectorizedNoMissingNoTable(blocks[0], i, blocks[1], j);
						engine.UnphasedVectorized(blocks[0], i, blocks[1], j);
						engine.UnphasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
						engine.Runlength(blocks[0], i, blocks[1], j);
						*/
						//(engine.*f[method])(blocks[0], i, blocks[1], j, &perfs[method]);
						engine.PhasedList(blocks[0],i,blocks[1],j,nullptr);
						//engine.AllAlgorithms(blocks[0],i,blocks[1],j,&perfs[method]);
					}
				}
				n_total += blocks[0].n_rec * blocks[1].n_rec;


				if(fl_lim++ == 10){
					std::cerr << "m=" << 666 << " " << utility::ToPrettyString(n_total) << " " << utility::ToPrettyString((uint64_t)((float)n_total/timer.Elapsed().count())) << "/s " << utility::ToPrettyString((uint64_t)((float)n_total*reader.hdr.GetNumberSamples()/timer.Elapsed().count())) << "/gt/s " << timer.ElapsedString() << " " << utility::ToPrettyString(engine.t_out) << std::endl;
					fl_lim = 0;
				}
			}

		}

		std::cerr << "all done" << std::endl;

		//for(int method = 0; method < 10; ++method){
		int method = 0;
			timer.Start();
			n_total = 0; fl_lim = 0;
			for(int b1 = 0; b1 < reader.index.n; ++b1){
				//ldd[b1].Inflate(reader.hdr.GetNumberSamples(), TWK_LDD_LIST);
				blocks[0] = ldd[b1];
				blocks[0].Inflate(reader.hdr.GetNumberSamples(), unpack_list[method]);

				for(int i = 0; i < ldd[b1].n_rec; ++i){
					for(int j = i+1; j < ldd[b1].n_rec; ++j){
						if(blocks[0].blk->rcds[i].ac + blocks[0].blk->rcds[j].ac < 5){
							continue;
						}

						if(blocks[0].blk->rcds[j].pos - blocks[0].blk->rcds[i].pos > dist){
							//std::cerr << "out of range continue=" << (blocks[0].blk->rcds[j].pos - blocks[0].blk->rcds[i].pos) << std::endl;
							continue;
						}

						/*
						engine.PhasedVectorized(blocks[0], i, blocks[1], j);
						engine.PhasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
						engine.PhasedVectorizedNoMissingNoTable(blocks[0], i, blocks[1], j);
						engine.UnphasedVectorized(blocks[0], i, blocks[1], j);
						engine.UnphasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
						engine.Runlength(blocks[0], i, blocks[1], j);
						*/
						//engine.AllAlgorithms(blocks[0],i,blocks[0],j,&perfs[method]);
						(engine.*f[method])(blocks[0], i, blocks[0], j, &perfs[method]);
					}

				}
				n_total += ((blocks[0].n_rec * blocks[0].n_rec) - blocks[0].n_rec) / 2; // n choose 2

				for(int b2 = b1+1; b2 < reader.index.n; ++b2){
					//ldd[b2].Inflate(reader.hdr.GetNumberSamples(), TWK_LDD_LIST);
					blocks[1] = ldd[b2];
					blocks[1].Inflate(reader.hdr.GetNumberSamples(), unpack_list[method]);

					if(blocks[1].blk->rcds[0].pos - blocks[0].blk->rcds[blocks[0].n_rec-1].pos > dist){
						std::cerr << "never overlap=" << blocks[1].blk->rcds[0].pos - blocks[0].blk->rcds[blocks[0].n_rec-1].pos << std::endl;
						goto out_of_range;
					}

					for(int i = 0; i < blocks[0].n_rec; ++i){
						for(int j = 0; j < blocks[1].n_rec; ++j){
							if( blocks[0].blk->rcds[i].ac + blocks[1].blk->rcds[j].ac < 5 ){
								continue;
							}

							if(blocks[1].blk->rcds[j].pos - blocks[0].blk->rcds[i].pos > dist){
								//std::cerr << "out of range continue=" << (blocks[1].blk->rcds[j].pos - blocks[0].blk->rcds[i].pos) << std::endl;
								break;
							}
							/*
							engine.PhasedVectorized(blocks[0], i, blocks[1], j);
							engine.PhasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
							engine.PhasedVectorizedNoMissingNoTable(blocks[0], i, blocks[1], j);
							engine.UnphasedVectorized(blocks[0], i, blocks[1], j);
							engine.UnphasedVectorizedNoMissing(blocks[0], i, blocks[1], j);
							engine.Runlength(blocks[0], i, blocks[1], j);
							*/
							(engine.*f[method])(blocks[0], i, blocks[1], j, &perfs[method]);
							//engine.AllAlgorithms(blocks[0],i,blocks[1],j,&perfs[method]);
						}
					}
					n_total += blocks[0].n_rec * blocks[1].n_rec;


					if(fl_lim++ == 10){
						std::cerr << "m=" << method << " " << utility::ToPrettyString(n_total) << " " << utility::ToPrettyString((uint64_t)((float)n_total/timer.Elapsed().count())) << "/s " << utility::ToPrettyString((uint64_t)((float)n_total*reader.hdr.GetNumberSamples()/timer.Elapsed().count())) << "/gt/s " << timer.ElapsedString() << " " << utility::ToPrettyString(engine.t_out) << std::endl;
						fl_lim = 0;
					}
				}
				out_of_range:
				continue;
			}

			std::cerr << "m=" << method << " " << utility::ToPrettyString(n_total) << " " << utility::ToPrettyString((uint64_t)((float)n_total/timer.Elapsed().count())) << "/s " << utility::ToPrettyString((uint64_t)((float)n_total*reader.hdr.GetNumberSamples()/timer.Elapsed().count())) << "/gt/s " << timer.ElapsedString() << " " << utility::ToPrettyString(engine.t_out) << std::endl;
		//}

		for(int i = 0; i < 10; ++i){
			for(int j = 0; j < perf_size; ++j){
				std::cout << i<<"\t"<<j<<"\t"<< (double)perfs[i].cycles[j]/perfs[i].freq[j] << "\t" << perfs[i].freq[j] << "\t" << perfs[i].cycles[j] << '\n';
			}
			delete[] perfs[i].cycles;
		}

		delete[] ldd;

		return true;
	}

public:
	twk_ld_settings settings;
};

}


#endif /* LD_H_ */
