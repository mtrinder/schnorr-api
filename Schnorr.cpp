#include "cryptopp/sha3.h"
#include "cryptopp/sha.h"
#include "Schnorr.h"

SchnorrCPP::CCurve::CCurve()
{
	secretKeySet = false;
	publicKeySet = false;

	// Load in curve secp256r1
	Integer p, a, b, Gx, Gy;

	// Create the group
	p = Integer("0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF");
	a = Integer("-3");
	b = Integer("0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B");
	q = Integer("0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551");
	Gx = Integer("0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296");
	Gy = Integer("0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5");

	// Store the curve and the generator
	ec = ECP(p, a, b);
	G = ECPPoint(Gx, Gy);
}

SchnorrCPP::CCurve::~CCurve()
{
	secretKeySet = false;
	publicKeySet = false;
}

bool SchnorrCPP::CCurve::GenerateSecretKey()
{
	secretKey = Integer(rng, 256) % q;
	secretKeySet = true;
	return true;
}

bool SchnorrCPP::CCurve::GeneratePublicKey()
{
	if (!secretKeySet)
		return false;
	Q = ec.ScalarMultiply(G, secretKey);
	publicKeySet = true;
	return true;
}

bool SchnorrCPP::CCurve::GenerateKeys()
{
	if (!GenerateSecretKey())
		return false;
	if (!GeneratePublicKey())
		return false;
	return true;
}

Integer SchnorrCPP::CCurve::GetPublicKeyX()
{
	return Q.x;
}

Integer SchnorrCPP::CCurve::GetPublicKeyY()
{
	return Q.y;
}

Integer SchnorrCPP::CCurve::GetSecretKey()
{
	return secretKey;
}

void SchnorrCPP::CCurve::ModuloAddToHex(Integer k, Integer i, std::vector<unsigned char>& dataBytes)
{
    Integer ki = (k + i).Modulo(q);

    ostringstream oss;
    oss << std::hex << ki;
    string str = oss.str();
    str = str.substr(0, str.size()-1);
    
    // Debug
    //cout << str << endl;

    const char* ptr = str.data();
    
    dataBytes = std::vector<unsigned char>(ptr, ptr + str.length());
}

void SchnorrCPP::CCurve::PointMultiplyAddToHex(Integer i, std::vector<unsigned char>& dataBytes)
{
    if (!publicKeySet)
        return;

    ECPPoint ki = ec.ScalarMultiply(G, i);
    
    ECPPoint kip = ECPPoint(ki.x + Q.x, ki.y + Q.y);
    
    // Debug
    //ostringstream oss;
    //oss << std::hex << kip.x;
    //string str = oss.str();
    //str = str.substr(0, str.size()-1);
    //cout << str << endl;
    
    const bool fCompressed = true;
    dataBytes.resize(ec.EncodedPointSize(fCompressed));
    ec.EncodePoint(&dataBytes[0], kip, fCompressed);
}

bool SchnorrCPP::CCurve::SetVchPublicKey(std::vector<unsigned char> vchPubKey)
{
	ECPPoint publicKey;

	if (!ec.DecodePoint (publicKey, &vchPubKey[0], vchPubKey.size()))
        return false;

    publicKeySet = true;
	Q = publicKey;
	return true;
}

bool SchnorrCPP::CCurve::GetVchPublicKey(std::vector<unsigned char>& vchPubKey)
{
    if (!publicKeySet)
        return false;
    
	// set to true for compressed
	const bool fCompressed = true;
	vchPubKey.resize(ec.EncodedPointSize(fCompressed));
	ec.EncodePoint(&vchPubKey[0], Q, fCompressed);
	
    return true;
}

bool SchnorrCPP::CCurve::SetVchSecretKey(std::vector<unsigned char> vchSecret)
{
	if (vchSecret.size() != SCHNORR_SECRET_KEY_SIZE)
	return false;

	secretKey.Decode(&vchSecret[0], SCHNORR_SECRET_KEY_SIZE);
    secretKeySet = true;
    
    GeneratePublicKey();
	return true;
}

bool SchnorrCPP::CCurve::GetVchSecretKey(std::vector<unsigned char>& vchSecret)
{
	if (!secretKeySet)
        return false;

	vchSecret.resize(SCHNORR_SECRET_KEY_SIZE);
	secretKey.Encode(&vchSecret[0], SCHNORR_SECRET_KEY_SIZE);
	return true;
}

Integer SchnorrCPP::CCurve::HashPointMessage(const ECPPoint& R,
                                             const byte* message, int mlen)
{
    const int digestsize = 256/8;
    SHA3 sha(digestsize);
    
    int len = ec.EncodedPointSize();
    byte *buffer = new byte[len];
    ec.EncodePoint(buffer, R, false);
    sha.Update(buffer, len);
    delete[] buffer;
    
    sha.Update(message, mlen);
    
    byte digest[digestsize];
    sha.Final(digest);
    
    Integer ans;
    ans.Decode(digest, digestsize);
    return ans;
}

void SchnorrCPP::CCurve::Sign(Integer& sigE, Integer& sigS, const byte* message, int mlen)
{
    Integer k;
    ECPPoint R;
    k = Integer(rng, 256) % q;
    R = ec.ScalarMultiply(G, k);
    sigE = HashPointMessage(R, message, mlen) % q;
    sigS = (k - secretKey*sigE) % q;
}

bool SchnorrCPP::CCurve::Verify(const Integer& sigE, const Integer& sigS,
                                const byte* message, int mlen)
{
    ECPPoint R;
    R = ec.CascadeScalarMultiply(G, sigS, Q, sigE);
    Integer sigEd = HashPointMessage(R, message, mlen) % q;
    return (sigE == sigEd);
}

bool SchnorrCPP::CCurve::GetSignatureFromVch(std::vector<unsigned char> vchSig, Integer& sigE, Integer& sigS)
{
    if (vchSig.size() != (SCHNORR_SIG_SIZE * 2))
        return false;
    
    // extract bytes
    std::vector<unsigned char> sigEVec(&vchSig[0], &vchSig[SCHNORR_SIG_SIZE]);
    std::vector<unsigned char> sigSVec(&vchSig[SCHNORR_SIG_SIZE], &vchSig[1 + SCHNORR_SIG_SIZE * 2]);
    
    // vectors -> Integers
    sigE.Decode(&sigEVec[0], SCHNORR_SIG_SIZE);
    sigS.Decode(&sigSVec[0], SCHNORR_SIG_SIZE);
    return true;
}

bool SchnorrCPP::CCurve::GetVchFromSignature(std::vector<unsigned char>& vchSig, Integer sigE, Integer sigS)
{
    vchSig.resize(SCHNORR_SIG_SIZE * 2);
    
    if (sigE.MinEncodedSize() > SCHNORR_SIG_SIZE || sigS.MinEncodedSize() > SCHNORR_SIG_SIZE)
        return false;
    
    sigE.Encode(&vchSig[0], SCHNORR_SIG_SIZE);
    sigS.Encode(&vchSig[SCHNORR_SIG_SIZE], SCHNORR_SIG_SIZE);
    return true;
}


