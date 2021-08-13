//import java.lang.management.ManagementFactory;
import java.util.Map;

//import javax.management.InstanceAlreadyExistsException;
import javax.management.JMX;
//import javax.management.MBeanServer;
import javax.management.MBeanServerConnection;
//import javax.management.MalformedObjectNameException;
//import javax.management.ObjectInstance;
//import javax.management.StandardMBean;
//import java.lang.Exception;
import java.net.MalformedURLException;
import java.io.IOException;
import java.beans.IntrospectionException;
import java.util.Arrays;
import java.util.Iterator;
import java.util.Set;
import java.util.TreeSet;
import java.util.Map;
import java.util.HashMap;
import javax.management.AttributeChangeNotification;
import javax.management.Notification;
import javax.management.NotificationEmitter;
import javax.management.NotificationListener;
import javax.management.ObjectName;
import javax.management.remote.JMXConnector;
import javax.management.remote.JMXConnectorFactory;
import javax.management.remote.JMXServiceURL;
import javax.management.MBeanInfo;
import javax.management.MBeanAttributeInfo;
import javax.management.AttributeNotFoundException;
import javax.management.RuntimeMBeanException;
import javax.management.InstanceNotFoundException;
import javax.management.MBeanException;
import javax.management.ReflectionException;


public class alligatorJmx
{
	public static int intMethod(int n) {
		System.out.println("hello world");
		return n*n;
	}

	public static boolean booleanMethod(boolean bool) {
		 return !bool;
	}

	public String getJmx(String arg, String metrics, String conf) throws Exception {
	//public String getJmx() throws Exception {
		System.out.println("arg is " + arg);
		if (arg == null)
			return "";

		String outstr = "";

		JMXServiceURL url;
		try {
			url = new JMXServiceURL(arg);
			//url = new JMXServiceURL("service:jmx:rmi:///jndi/rmi://:12345/jmxrmi");
		}
		catch (MalformedURLException e) {
			System.out.println("Failed to create JMXServiceURL for " + arg + ". Error: "+ e);
			return "";
		}
		JMXConnector jmxc;
		try {
			jmxc = JMXConnectorFactory.connect(url, null);
		}
		catch (IOException e)
		{
			System.out.println("Failed to create JMXServiceURL for " + arg + ". Error: " + e);
			return "";
		}

		MBeanServerConnection mbsc;
		try {
			mbsc = jmxc.getMBeanServerConnection();
		}
		catch (IOException e)
		{
			System.out.println("Failed to create JMXServiceURL for " + arg + ". Error: "+ e);
			return "";
		}


		//System.out.println("Domains:");
		String domains[];
		try {
			domains = mbsc.getDomains();
		}
		catch (IOException e) {
			System.out.println("Failed to create getDomains for " + arg + ". Error: "+ e);
			return "";
		}

		Arrays.sort(domains);
		for (String domain : domains) {
			//System.out.println("\tDomain = " + domain);
		}

		//System.out.println("MBeanServer default domain = " + mbsc.getDefaultDomain());
		//System.out.println("\nMBean count = " + mbsc.getMBeanCount());
		//System.out.println("\nQuery MBeanServer MBeans:");

		Set<ObjectName> names;
		try {
			names = new TreeSet<ObjectName>(mbsc.queryNames(null, null));
		} catch (IOException e) {
				//System.out.println("Failed to getMBeanInfo. Error: "+ e);
				return "";
		}
		for (ObjectName name : names) {
			//System.out.println("==============================");
			//System.out.println("ObjectName: '" + name + "'");

			MBeanInfo info;
			try {
				info = mbsc.getMBeanInfo(name);
			} catch (InstanceNotFoundException e) {
				return "";
			}
			//try {
			//	info = mbsc.getMBeanInfo(name);
			//} catch (IntrospectionException e) {
			//	System.out.println("Failed to getMBeanInfo. Error: "+ e);
			//	return "";
			//} catch (InstanceNotFoundException e) {
			//	System.out.println("Failed to getMBeanInfo. Error: "+ e);
			//	return "";
			//}
			MBeanAttributeInfo[] attrInfo = info.getAttributes();
			Map<String, Object> map = new HashMap<>();
			for (MBeanAttributeInfo attr : attrInfo) {
				if (attr.isReadable()) {
					String AttrName = attr.getName();
					Object Attr = null;
					try {
						Attr = mbsc.getAttribute(name, AttrName);
						//System.out.println("\t'" + AttrName + "': '" + Attr +"'");
						//outstr += "{\"object\": \"" + name + "\", \"attribute name\": \"" + AttrName + "\", \"value\": \"" + Attr + "\"},\n";
						outstr += AttrName + ":" + Attr + "#object:" + name + "\n";
					} catch (IOException e) {
						System.out.println(e.getMessage());
					} catch (RuntimeMBeanException e) {
						System.err.println(e.getMessage());
					} catch (AttributeNotFoundException e) {
						System.err.println(e.getMessage());
					} catch (InstanceNotFoundException e) {
						System.err.println(e.getMessage());
					} catch (MBeanException e) {
						System.err.println(e.getMessage());
					} catch (ReflectionException e) {
						System.err.println(e.getMessage());
					}
				}
			}
		}
		
		try {
			jmxc.close();
		}
		catch (IOException e) {
			System.out.println("Failed to close " + arg + ". Error: "+ e);
			return "";
		}


		return outstr;
		//String host = "localhost";
		//int port = 1234;
		//String url = "service:jmx:rmi:///jndi/rmi://" + host + ":" + port + "/jmxrmi";
		//JMXServiceURL serviceUrl;
		//JMXConnector jmxConnector;
		//try {
		//	serviceUrl = new JMXServiceURL(url);
		//}
		//catch (MalformedURLException e) {
		//	System.out.println("Failed to create JMXServiceURL for " + host + ","+ port+ ". Error: "+ e);
		//	return;
		//	//return "";
		//}
	}
}
