import java.io.FileInputStream;
import java.security.KeyStore;
import java.util.Enumeration;
import java.io.IOException;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.UnrecoverableEntryException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.Date;
import java.text.SimpleDateFormat;
import java.util.*;
import java.math.BigInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;

public class alligatorJks {

	public String getJks(String FileName, String Password) throws Exception {

		String outstr = "";

		try {
			KeyStore keystore = KeyStore.getInstance(KeyStore.getDefaultType());
			keystore.load(new FileInputStream(FileName), Password.toCharArray());
			Enumeration aliases = keystore.aliases();
			for(; aliases.hasMoreElements();) {
				String alias = (String) aliases.nextElement();

				Date certExpiryDate = ((X509Certificate) keystore.getCertificate(alias)).getNotAfter();
				Date certStartDate = ((X509Certificate) keystore.getCertificate(alias)).getNotBefore();
				BigInteger serialNumber = ((X509Certificate) keystore.getCertificate(alias)).getSerialNumber();
				String Serial = serialNumber.toString(16);
				String Subject = ((X509Certificate) keystore.getCertificate(alias)).getSubjectDN().getName();
				String Issuer = ((X509Certificate) keystore.getCertificate(alias)).getIssuerDN().getName();

				SimpleDateFormat ft = new SimpleDateFormat ("yyyy-MM-dd HH:mm:ss");
				Date today = new Date();
				long dateDiff = certExpiryDate.getTime() - today.getTime();
				long expiresIn = dateDiff / (24 * 60 * 60 * 1000);
				long expireDate = certExpiryDate.getTime() / (1000);
				long startDate = certStartDate.getTime() / (1000);
				//System.out.println("Certifiate: " + alias + "\tExpires On: " + certExpiryDate + "\tFormated Date: " + ft.format(certExpiryDate) + "\tToday's Date: " + ft.format(today) + "\tExpires In: "+ expiresIn + "\tExpiry date: " + expireDate);

				String appendstr = "#CN:" + alias + ",cert:" + FileName + ",serial:" + Serial + ",subject:" + Subject + ",issuer:" + Issuer;

				outstr += "x509_cert_expire_days" + ":" + expiresIn + appendstr + "\n";
				outstr += "x509_cert_not_after" + ":" + expireDate + appendstr + "\n";
				outstr += "x509_cert_not_Before" + ":" + startDate + appendstr + "\n";
			}
		}

		catch (Exception e) {
			e.printStackTrace();
		}

		return outstr;
	}

	public String walkJks(String argv, String metrics, String conf) throws Exception {
		//String retstring = "LOL";
		if (argv == null)
		{
			System.out.println("argv is '" + argv + "': return from walkJks");
			return "";
		}
		else if (argv.isEmpty())
		{
			System.out.println("argv is '" + argv + "': return from walkJks");
			return "";
		}
		else
			System.out.println("argv is '" + argv + "'");

		String[] value = { "" };

		String[] argSplited = argv.split(" ");
		if (argSplited.length == 0)
		{
			System.out.println("not valid string arg, return from walkJks");
			return "";
		}

		String dir = argSplited[0];
		String match = argSplited[1];
		String password = argSplited[2];

		Files.list(new File(dir).toPath()).forEach(path -> {
			System.out.println(path);
			String StrPath = path.toAbsolutePath().toString();
			if (StrPath.contains(match)) {
				try {
					value[0] += getJks(StrPath, password);
				}
				catch (Exception e) {
					e.printStackTrace();
				}
			}
		});

		return value[0];
		//return retstring;
	}

	//public static void main(String[] argv) throws Exception {
	//	System.out.println(walkJks("/var/lib .jks password"));
	//}
}

