#ifndef HSLL_NONCOPYABLE
#define HSLL_NONCOPYABLE

namespace HSLL
{

    /**
     * @brief A base class to prevent copying of derived classes
     *
     * @note Inherit from this class to make your class non-copyable.
     *       This is useful for classes that manage resources where copying
     *       would be unsafe or meaningless.
     */
    class noncopyable
    {
    public:
        noncopyable(const noncopyable &) = delete;
        void operator=(const noncopyable &) = delete;

    protected:
        noncopyable() = default;
        ~noncopyable() = default;
    };
}

#endif // HSLL_NONCOPYABLE