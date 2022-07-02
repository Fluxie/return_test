#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

/// De-optimization trick from Google Benchmark.
template<class Tp>
inline __attribute__((always_inline)) void DoNotOptimize( Tp const &value )
{
    asm volatile("" : : "r,m"(value) : "memory");
}

/// De-optimization trick from Google Benchmark.
template<class Tp>
inline __attribute__((always_inline)) void DoNotOptimize( Tp &value )
{
#if defined(__clang__)
    asm volatile("" : "+r,m"(value) : : "memory");
#else
    asm volatile("" : "+m,r"(value) : : "memory");
#endif
}

/// Defines the data type used to return the buffer after the transformation.
enum class ReturnMethod
{
    Array,
    Vector
};

/// Is exception handling used during the test?
enum class ExceptionHandling
{
    Omit,
    Include,
};

/// A small buffer allocated from the stack.
template<size_t TSize>
class StackBuffer
{
public:

    /// Initializes the buffer from the given byte array.
    constexpr StackBuffer(
            const std::byte *buffer,
            size_t length
    ) noexcept:
            m_length( std::min( TSize, length ))
    {
        std::memcpy( m_data.data(), buffer, m_length );
    }

    StackBuffer(
            const StackBuffer &source
    ) noexcept:
            m_length( source.m_length )
    {
        std::memcpy( m_data.data(), source.m_data.data(), source.m_length );
    }

    StackBuffer &operator=(
            const StackBuffer &source
    ) noexcept
    {
        if( this != &source ) {
            m_length = source.m_length;
            std::memcpy( m_data.data(), source.m_data.data(), source.m_length );
        }
    }

    StackBuffer(
            StackBuffer &&source
    ) noexcept:
            m_length( source.m_length )
    {
        std::memcpy( m_data.data(), source.m_data.data(), source.m_length );

        // Reminder if this class is to be used as an example for further processing.
        static_assert( std::is_trivially_destructible<std::byte>::value,
                "Ensure the items in the source array are properly moved." );
    }

    StackBuffer &operator=(
            StackBuffer &&source
    ) noexcept
    {
        if( this != &source ) {
            m_length = source.m_length;
            std::memcpy( m_data.data(), source.m_data.data(), source.m_length );

            // Reminder if this class is to be used as an example for further processing.
            static_assert( std::is_trivially_destructible<std::byte>::value,
                    "Ensure the items in the source array are properly moved." );
        }
    }


private:

    std::array<std::byte, TSize> m_data;
    size_t m_length;
};

/// Generates test data.
std::vector<std::byte> GenerateData(
        size_t length
)
{
    // Seed with a real random value, if available
    std::random_device r;

    // Choose a random numbers that fit std::byte.
    std::default_random_engine engine( r());
    std::uniform_int_distribution<int> uniform_dist( 0, 255 );

    // Generate the buffer.
    std::vector<std::byte> vecData;
    vecData.reserve( length );
    while( vecData.size() < length )
        vecData.push_back( static_cast< std::byte >( uniform_dist( engine )));
    return vecData;
}

/// Transforms the input buffer into either std::array or std::vector depending on the template parameters.
template<size_t TSize, ReturnMethod TReturnMethod>
std::variant<StackBuffer<TSize>, std::vector<std::byte>> Transform(
        const std::byte *buffer,
        size_t length
)
{
    // Choose data type.
    if( TReturnMethod == ReturnMethod::Array ) {
        return StackBuffer<TSize>( buffer, length );
    } else if( TReturnMethod == ReturnMethod::Vector ) {
        return std::vector<std::byte>( buffer, buffer + length );
    }
}

/// Adds a try-catch block to the byte array transformation depending on the TExceptionHandling tepmlate parameter.
template<size_t TSize, ReturnMethod TReturnMethod, ExceptionHandling TExceptionHandling>
std::variant<StackBuffer<TSize>, std::vector<std::byte>> TryTransform(
        const std::byte *buffer,
        size_t length
)
{
    // Include catch block in the call chain?
    if( TExceptionHandling == ExceptionHandling::Include ) {
        try {
            return Transform<TSize, TReturnMethod>( buffer, length );
        }
        catch (std::bad_alloc &) {
            return std::vector<std::byte>();
        }
    } else if( TExceptionHandling == ExceptionHandling::Omit ) {
        return Transform<TSize, TReturnMethod>( buffer, length );
    }
}

/// Executes multiple byte butter into requested data type transformations in a loop to increase processing time.
template<size_t TSize, ReturnMethod TReturnMethod, ExceptionHandling TExceptionHandling>
int TryMultipleTransforms(
        const std::byte *buffer,
        size_t length
)
{
    // Do enough iterations to hide time needed for the actual measurement.
    const int scaling = 10000;
    for( int i = 0; i < scaling; ++i ) {
        // Try to limit compiler optimizations.
        DoNotOptimize( TryTransform<TSize, TReturnMethod, TExceptionHandling>( buffer, length ));
    }
    return scaling;
}

template<size_t TSize, ReturnMethod TReturnMethod, ExceptionHandling TExceptionHandling>
std::tuple<std::chrono::nanoseconds, bool> MeasureTransform(
        const std::vector<std::byte> &vecData
)
{
    // Do multiple tests with each test set to account for random variance.
    std::vector<std::chrono::nanoseconds> vecSamples;
    vecSamples.reserve( 40000 );
    while( vecSamples.size() < 1000 ) {
        // Dp a single test.
        auto tpStart = std::chrono::high_resolution_clock::now();
        int scaling = TryMultipleTransforms<TSize, TReturnMethod, TExceptionHandling>( vecData.data(), vecData.size());
        auto tpEnd = std::chrono::high_resolution_clock::now();
        auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>( tpEnd - tpStart );
        vecSamples.push_back( nanoseconds / scaling );
    }
    std::sort( vecSamples.begin(), vecSamples.end());

    // Return the median of the results.
    return std::make_tuple( vecSamples[ vecSamples.size() / 2 ], TReturnMethod == ReturnMethod::Vector );
}

/// Executes a test and reports the results to the console.
template<size_t TSize, ReturnMethod TReturnMethod, ExceptionHandling TExceptionHandling>
void MeasureAndReport(
        const std::vector<std::byte> &vecData
)
{
    // Skip measurement if the data would not fit into the array.
    // The array truncates the test data to guarantee it fits.
    // This falsifies the results.
    if( TSize < vecData.size())
        return;

    auto testResult = MeasureTransform<TSize, TReturnMethod, TExceptionHandling>( vecData );
    std::cout << "Data: " << vecData.size() << ", Buffer: " << TSize << ", Duration:"
              << std::get<0>( testResult ).count() <<
              " ns, Vector: " << std::get<1>( testResult ) << ", Exceptions: "
              << static_cast< uint32_t >( TExceptionHandling ) << std::endl;
}


int main()
{

    // Generate test vectors.
    std::vector<std::vector<std::byte> > test_vectors;
    test_vectors.reserve( 1000 );
    for( size_t s = 1; s <= 32; ++s )
        test_vectors.push_back( GenerateData( s * s ));

    // Run benchmarks with different return sizes.
    for( const std::vector<std::byte> &test_vector: test_vectors ) {
        // Run test with different buffer sizes.
        MeasureAndReport<1, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<1, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<4, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<4, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<10, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<10, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<64, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<64, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<64, ReturnMethod::Array, ExceptionHandling::Omit>( test_vector );
        MeasureAndReport<64, ReturnMethod::Vector, ExceptionHandling::Omit>( test_vector );
        MeasureAndReport<128, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<128, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<256, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<256, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<512, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<512, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<1024, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<1024, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<2048, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<2048, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<4096, ReturnMethod::Array, ExceptionHandling::Include>( test_vector );
        MeasureAndReport<4096, ReturnMethod::Vector, ExceptionHandling::Include>( test_vector );
//        MeasureAndReport<8192, false>( test_vector );
//        MeasureAndReport<16374, false>( test_vector );
//        MeasureAndReport<16374*2, false>( test_vector );
//        MeasureAndReport<16374*4, false>( test_vector );
    }

    return 0;
}
