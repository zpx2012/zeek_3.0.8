// See the file "COPYING" in the main distribution directory for copyright.

// Common base class for the X509 and OCSP analyzer, which share a fair amount of
// code

#ifndef FILE_ANALYSIS_X509_COMMON
#define FILE_ANALYSIS_X509_COMMON

#include "file_analysis/File.h"
#include "file_analysis/Analyzer.h"

#include <openssl/x509.h>
#include <openssl/asn1.h>

namespace file_analysis {

class X509Common : public file_analysis::Analyzer {
public:
	~X509Common() override {};

	/**
	 * Retrieve an X509 extension value from an OpenSSL BIO to which it was
	 * written.
	 *
	 * @param bio the OpenSSL BIO to read. It will be freed by the function,
	 * including when an error occurs.
	 *
	 * @param f an associated file, if any (used for error reporting).
	 *
	 * @return The X509 extension value.
	 */
	static StringVal* GetExtensionFromBIO(BIO* bio, File* f = 0);

	static double GetTimeFromAsn1(const ASN1_TIME* atime, File* f, Reporter* reporter);

protected:
	X509Common(file_analysis::Tag arg_tag, RecordVal* arg_args, File* arg_file);

	void ParseExtension(X509_EXTENSION* ex, EventHandlerPtr h, bool global);
	void ParseSignedCertificateTimestamps(X509_EXTENSION* ext);
	virtual void ParseExtensionsSpecific(X509_EXTENSION* ex, bool, ASN1_OBJECT*, const char*) = 0;
};

}

#endif /* FILE_ANALYSIS_X509_COMMON */
