/*
 * Copyright (C) 2023 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2,1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "CryptoKeyOKP.h"

#if ENABLE(WEB_CRYPTO)

#include "CryptoKeyPair.h"
#include "GCryptUtilities.h"
#include "JsonWebKey.h"
#include <pal/crypto/gcrypt/Utilities.h>
#include <pal/crypto/tasn1/Utilities.h>
#include <wtf/text/Base64.h>

namespace WebCore {

bool CryptoKeyOKP::isPlatformSupportedCurve(NamedCurve namedCurve)
{
    return namedCurve == NamedCurve::Ed25519;
}

namespace CryptoKeyOKPImpl {

static bool supportedAlgorithmIdentifier(CryptoAlgorithmIdentifier keyIdentifier, const Vector<uint8_t>& identifier)
{
    auto* data = identifier.data();
    auto size = identifier.size();

    switch (keyIdentifier) {
    case CryptoAlgorithmIdentifier::Ed25519:
        // Ed25519 only supports id-Ed25519 idenfitied for imported keys.
        if (CryptoConstants::matches(data, size, CryptoConstants::s_ed25519Identifier))
            return true;
        return false;
    default:
        ASSERT_NOT_REACHED();
        break;
    }

    return false;
}

}

std::optional<CryptoKeyPair> CryptoKeyOKP::platformGeneratePair(CryptoAlgorithmIdentifier identifier, NamedCurve namedCurve, bool extractable, CryptoKeyUsageBitmap usages)
{
    if (namedCurve != NamedCurve::Ed25519)
        return { };

    PAL::GCrypt::Handle<gcry_sexp_t> genkeySexp;
    gcry_error_t error = gcry_sexp_build(&genkeySexp, nullptr, "(genkey (ecdsa (curve \"Ed25519\") (flags eddsa)))");
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    PAL::GCrypt::Handle<gcry_sexp_t> keyPairSexp;
    error = gcry_pk_genkey(&keyPairSexp, genkeySexp);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    PAL::GCrypt::Handle<gcry_mpi_t> qMpi, dMpi;
    error = gcry_sexp_extract_param(keyPairSexp, "private-key", "qd", &qMpi, &dMpi, nullptr);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return std::nullopt;
    }

    auto publicKeyData = mpiData(qMpi);
    auto privateKeyData = mpiData(dMpi);
    if (!publicKeyData || publicKeyData->size() != 32 || !privateKeyData || privateKeyData->size() != 32)
        return std::nullopt;

    bool isPublicKeyExtractable = true;
    auto publicKey = CryptoKeyOKP::create(identifier, namedCurve, CryptoKeyType::Public, Vector<uint8_t>(*publicKeyData), isPublicKeyExtractable, usages);
    ASSERT(publicKey);
    auto privateKey = CryptoKeyOKP::create(identifier, namedCurve, CryptoKeyType::Private, Vector<uint8_t>(*privateKeyData), extractable, usages);
    ASSERT(privateKey);
    return CryptoKeyPair { WTFMove(publicKey), WTFMove(privateKey) };
}

// Per https://www.ietf.org/rfc/rfc5280.txt
// SubjectPublicKeyInfo ::= SEQUENCE { algorithm AlgorithmIdentifier, subjectPublicKey BIT STRING }
// AlgorithmIdentifier  ::= SEQUENCE { algorithm OBJECT IDENTIFIER, parameters ANY DEFINED BY algorithm OPTIONAL }
// Per https://www.rfc-editor.org/rfc/rfc8410
// id-X25519    OBJECT IDENTIFIER ::= { 1 3 101 110 }
// id-X448      OBJECT IDENTIFIER ::= { 1 3 101 111 }
// id-Ed25519   OBJECT IDENTIFIER ::= { 1 3 101 112 }
// id-Ed448     OBJECT IDENTIFIER ::= { 1 3 101 113 }
// For all of the OIDs, the parameters MUST be absent.
RefPtr<CryptoKeyOKP> CryptoKeyOKP::importSpki(CryptoAlgorithmIdentifier identifier, NamedCurve curve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap usages)
{
    // Decode the `SubjectPublicKeyInfo` structure using the provided key data.
    PAL::TASN1::Structure spki;
    if (!PAL::TASN1::decodeStructure(&spki, "WebCrypto.SubjectPublicKeyInfo", keyData))
        return nullptr;

    // Validate `algorithm.algorithm`.
    {
        auto algorithm = PAL::TASN1::elementData(spki, "algorithm.algorithm");
        if (!algorithm)
            return nullptr;

        if (!CryptoKeyOKPImpl::supportedAlgorithmIdentifier(identifier, *algorithm))
            return nullptr;
    }

    // Retrieve the `subjectPublicKey` data and embed it into the `public-key` s-expression.
    PAL::GCrypt::Handle<gcry_sexp_t> platformKey;
    {
        auto subjectPublicKey = PAL::TASN1::elementData(spki, "subjectPublicKey");
        if (!subjectPublicKey)
            return nullptr;

        // If the parameters field of the algorithm AlgorithmIdentifier field of spki is present, then throw a DataError.
        auto parameters = PAL::TASN1::elementData(spki, "algorithm.parameters");
        if (parameters)
            return nullptr;

        // Construct the `public-key` expression to be used for generating the MPI structure.
        gcry_error_t error = gcry_sexp_build(&platformKey, nullptr, "(public-key(ecc(curve Ed25519)(q %b)))",
            subjectPublicKey->size(), subjectPublicKey->data());
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return nullptr;
        }
    }

    // Exrtact the 'q' parameter to get the raw public key.
    PAL::GCrypt::Handle<gcry_mpi_t> mpi;
    gcry_error_t error = gcry_sexp_extract_param(platformKey, "public-key", "q", &mpi, nullptr);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return nullptr;
    }

    auto rawKey = mpiData(mpi);
    if (!rawKey)
        return nullptr;

    return create(identifier, curve, CryptoKeyType::Public, Vector<uint8_t>(*rawKey), extractable, usages);
}

ExceptionOr<Vector<uint8_t>> CryptoKeyOKP::exportSpki() const
{
    if (type() != CryptoKeyType::Public)
        return Exception { InvalidAccessError };

    PAL::TASN1::Structure spki;
    {
        // Create the `SubjectPublicKeyInfo` structure.
        if (!PAL::TASN1::createStructure("WebCrypto.SubjectPublicKeyInfo", &spki))
            return Exception { OperationError };

        // Write out the id-edPublicKey identifier under `algorithm.algorithm`.
        if (!PAL::TASN1::writeElement(spki, "algorithm.algorithm", CryptoConstants::s_ed25519Identifier.data(), 1))
            return Exception { OperationError };

        // The 'paramaters' element should not be present
        if (!PAL::TASN1::writeElement(spki, "algorithm.parameters", nullptr, 0))
            return Exception { OperationError };

        // Write out the public key data under `subjectPublicKey`. Because this is a
        // bit string parameter, the data size has to be multiplied by 8.
        if (!PAL::TASN1::writeElement(spki, "subjectPublicKey", m_data.data(), m_data.size() * 8))
            return Exception { OperationError };
    }

    // Retrieve the encoded `SubjectPublicKeyInfo` data and return it.
    auto result = PAL::TASN1::encodedData(spki, "");
    if (!result)
        return Exception { OperationError };

    return WTFMove(result.value());
}

// Per https://www.ietf.org/rfc/rfc5280.txt
// PrivateKeyInfo ::= SEQUENCE { version INTEGER, privateKeyAlgorithm AlgorithmIdentifier, privateKey OCTET STRING }
// AlgorithmIdentifier  ::= SEQUENCE { algorithm OBJECT IDENTIFIER, parameters ANY DEFINED BY algorithm OPTIONAL }
// Per https://www.rfc-editor.org/rfc/rfc8410
// id-X25519    OBJECT IDENTIFIER ::= { 1 3 101 110 }
// id-X448      OBJECT IDENTIFIER ::= { 1 3 101 111 }
// id-Ed25519   OBJECT IDENTIFIER ::= { 1 3 101 112 }
// id-Ed448     OBJECT IDENTIFIER ::= { 1 3 101 113 }
// For all of the OIDs, the parameters MUST be absent.
RefPtr<CryptoKeyOKP> CryptoKeyOKP::importPkcs8(CryptoAlgorithmIdentifier identifier, NamedCurve curve, Vector<uint8_t>&& keyData, bool extractable, CryptoKeyUsageBitmap usages)
{
    // Decode the `PrivateKeyInfo` structure using the provided key data.
    PAL::TASN1::Structure pkcs8;
    if (!PAL::TASN1::decodeStructure(&pkcs8, "WebCrypto.PrivateKeyInfo", keyData))
        return nullptr;

    // Validate `version`.
    {
        auto version = PAL::TASN1::elementData(pkcs8, "version");
        if (!version)
            return nullptr;

        if (!CryptoConstants::matches(version->data(), version->size(), CryptoConstants::s_asn1Version0))
            return nullptr;
    }

    // Validate `privateKeyAlgorithm.algorithm`.
    {
        auto algorithm = PAL::TASN1::elementData(pkcs8, "privateKeyAlgorithm.algorithm");
        if (!algorithm)
            return nullptr;

        if (!CryptoKeyOKPImpl::supportedAlgorithmIdentifier(identifier, *algorithm))
            return nullptr;
    }

    // If the parameters field of the algorithm AlgorithmIdentifier field of pkcs8 is present, then throw a DataError.
    auto parameters = PAL::TASN1::elementData(pkcs8, "privateKeyAlgorithm.parameters");
    if (parameters)
        return nullptr;

    // Decode the `CurvePrivateKey` structure using the `privateKey` data.
    PAL::TASN1::Structure ecPrivateKey;
    {
        auto privateKey = PAL::TASN1::elementData(pkcs8, "privateKey");
        if (!privateKey)
            return nullptr;

        if (!PAL::TASN1::decodeStructure(&ecPrivateKey, "WebCrypto.CurvePrivateKey", *privateKey))
            return nullptr;
    }

    // Retrieve the `privateKey.privateKey` data and embed it into the `private-key` s-expression.
    PAL::GCrypt::Handle<gcry_sexp_t> platformKey;
    {
        auto privateKey = PAL::TASN1::elementData(ecPrivateKey, "");
        if (!privateKey)
            return nullptr;

        // Validate the size of `privateKey`, making sure it fits the byte-size of the specified EC curve.
        if (privateKey->size() != 32)
            return nullptr;

        // Construct the `private-key` expression that will also be used for the EC context.
        gcry_error_t error = gcry_sexp_build(&platformKey, nullptr, "(private-key(ecc(curve Ed25519)(flags eddsa)(d %b)))",
            privateKey->size(), privateKey->data());
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return nullptr;
        }

        // Create an EC context for the specified curve.
        PAL::GCrypt::Handle<gcry_ctx_t> context;
        error = gcry_mpi_ec_new(&context, platformKey, nullptr);
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return nullptr;
        }

        // Retrieve the `q` point. If the public key was provided through the PKCS#8 import, that
        // key value will be retrieved as an gcry_mpi_point_t. Otherwise, the `q` point value will
        // be computed on-the-fly by libgcrypt for the specified elliptic curve.
        PAL::GCrypt::Handle<gcry_mpi_point_t> point(gcry_mpi_ec_get_point("q", context, 1));
        if (!point)
            return nullptr;

        // Bail if the retrieved `q` MPI point is not on the specified EC curve.
        if (!gcry_mpi_ec_curve_point(point, context))
            return nullptr;

        PAL::GCrypt::Handle<gcry_mpi_t> mpi;
        error = gcry_sexp_extract_param(platformKey, "private-key", "d", &mpi, nullptr);
        if (error != GPG_ERR_NO_ERROR) {
            PAL::GCrypt::logError(error);
            return nullptr;
        }

        auto rawKey = mpiData(mpi);
        if (!rawKey)
            return nullptr;

        return create(identifier, curve, CryptoKeyType::Private, Vector<uint8_t>(*rawKey), extractable, usages);
    }
}

ExceptionOr<Vector<uint8_t>> CryptoKeyOKP::exportPkcs8() const
{
    if (type() != CryptoKeyType::Private)
        return Exception { InvalidAccessError };

    PAL::TASN1::Structure ecPrivateKey;
    {
        // Create the `ECPrivateKey` structure.
        if (!PAL::TASN1::createStructure("WebCrypto.CurvePrivateKey", &ecPrivateKey))
            return Exception { OperationError };

        // Write out the data under `privateKey`.
        if (!PAL::TASN1::writeElement(ecPrivateKey, "", m_data.data(), m_data.size()))
            return Exception { OperationError };
    }

    PAL::TASN1::Structure pkcs8;
    {
        // Create the `PrivateKeyInfo` structure.
        if (!PAL::TASN1::createStructure("WebCrypto.PrivateKeyInfo", &pkcs8))
            return Exception { OperationError };

        // Write out '0' under `version`.
        if (!PAL::TASN1::writeElement(pkcs8, "version", "0", 0))
            return Exception { OperationError };

        // Write out the id-Ed25519 identifier under `privateKeyAlgorithm.algorithm`.
        if (!PAL::TASN1::writeElement(pkcs8, "privateKeyAlgorithm.algorithm", CryptoConstants::s_ed25519Identifier.data(), 1))
            return Exception { OperationError };

        // The 'paramaters' element should not be present
        if (!PAL::TASN1::writeElement(pkcs8, "privateKeyAlgorithm.parameters", nullptr, 0))
            return Exception { OperationError };

        // Write out the `CurvePrivateKey` data under `privateKey`.
        {
            auto data = PAL::TASN1::encodedData(ecPrivateKey, "");
            if (!data || !PAL::TASN1::writeElement(pkcs8, "privateKey", data->data(), data->size()))
                return Exception { OperationError };
        }

        // Eliminate the optional `attributes` element.
        if (!PAL::TASN1::writeElement(pkcs8, "attributes", nullptr, 0))
            return Exception { OperationError };
    }

    // Retrieve the encoded `PrivateKeyInfo` data and return it.
    auto result = PAL::TASN1::encodedData(pkcs8, "");
    if (!result)
        return Exception { OperationError };

    return WTFMove(result.value());
}

String CryptoKeyOKP::generateJwkD() const
{
    ASSERT(type() == CryptoKeyType::Private);
    return base64URLEncodeToString(m_data);
}

String CryptoKeyOKP::generateJwkX() const
{
    if (type() == CryptoKeyType::Public)
        return base64URLEncodeToString(m_data);

    ASSERT(type() == CryptoKeyType::Private);

    // We get an sexp of the private-key so that we could later extract the public-key associated to it.
    PAL::GCrypt::Handle<gcry_sexp_t> privKey;
    gcry_error_t error = gcry_sexp_build(&privKey, nullptr, "(private-key(ecc(curve Ed25519)(flags eddsa)(d %b)))", m_data.size(), m_data.data());
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return { };
    }

    // Create a context based on the private-key sexp.
    PAL::GCrypt::Handle<gcry_ctx_t> context;
    error = gcry_mpi_ec_new(&context, privKey, nullptr);
    if (error != GPG_ERR_NO_ERROR) {
        PAL::GCrypt::logError(error);
        return { };
    }

    // Return an EdDSA style compressed point. This is only supported for Twisted Edwards curves.
    PAL::GCrypt::Handle<gcry_mpi_t> qMPI(gcry_mpi_ec_get_mpi("q@eddsa", context, 0));
    if (qMPI) {
        auto q = mpiData(qMPI);
        if (q && q->size() == 32)
            return base64URLEncodeToString(*q);
    }

    return { };
}

Vector<uint8_t> CryptoKeyOKP::platformExportRaw() const
{
    return m_data;
}

} // namespace WebCore

#endif // ENABLE(WEB_CRYPTO)
