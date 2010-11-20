#include "datacarddevice.h"
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>

using namespace TelEngine;


static int opentty (char* dev)
{
	int		fd;
	struct termios	term_attr;

	fd = open (dev, O_RDWR | O_NOCTTY);

	if (fd < 0)
	{
		Debug("opentty",DebugAll, "Unable to open '%s'\n", dev);
		return -1;
	}

	if (tcgetattr (fd, &term_attr) != 0)
	{
		Debug("opentty",DebugAll, "tcgetattr() failed '%s'\n", dev);
		return -1;
	}

	term_attr.c_cflag = B115200 | CS8 | CREAD | CRTSCTS;
	term_attr.c_iflag = 0;
	term_attr.c_oflag = 0;
	term_attr.c_lflag = 0;
	term_attr.c_cc[VMIN] = 1;
	term_attr.c_cc[VTIME] = 0;

	if (tcsetattr (fd, TCSAFLUSH, &term_attr) != 0)
	{
		Debug("opentty",DebugAll,"tcsetattr() failed '%s'\n", dev);
	}

	return fd;
}


MonitorThread::MonitorThread(CardDevice* dev):m_device(dev)
{
}
MonitorThread::~MonitorThread()
{
}

void MonitorThread::run()
{
    at_res_t	at_res;
    at_queue_t*	e;
    int t;
//    int res;
//    struct iovec iov[2];
    int iovcnt;
//    size_t size;
//    size_t i = 0;

    /* start datacard initilization with the AT request */
    if (!m_device) 
        return;
    
    m_device->m_mutex.lock();

    m_device->timeout = 10000;

    if (m_device->at_send_at() || m_device->at_fifo_queue_add(CMD_AT, RES_OK))
    {
        Debug(DebugAll, "[%s] Error sending AT\n", m_device->c_str());
        m_device->disconnect();
        m_device->m_mutex.unlock();
        return;
    }

    m_device->m_mutex.unlock();

    // Main loop
    while (m_device->isRunning())
    {
        m_device->m_mutex.lock();

        if (m_device->dataStatus() || m_device->audioStatus())
        {
            Debug(DebugAll, "Lost connection to Datacard %s\n", m_device->c_str());
//		goto e_cleanup;
            m_device->disconnect();
            m_device->m_mutex.unlock();
            return;
        }
        t = m_device->timeout;

        m_device->m_mutex.unlock();


        if (!m_device->at_wait(&t))
        {
            m_device->m_mutex.lock();
            if (!m_device->initialized)
            {
                Debug(DebugAll, "[%s] timeout waiting for data, disconnecting\n", m_device->c_str());

//                if ((e = static_cast<at_queue_t*>(m_device->m_atQueue.get())))
		if ((e = m_device->at_fifo_queue_head()))
                {
                    Debug(DebugAll, "[%s] timeout while waiting '%s' in response to '%s'\n", m_device->c_str(), m_device->at_res2str (e->res), m_device->at_cmd2str (e->cmd));
                }

//		goto e_cleanup;
                Debug(DebugAll, "Error initializing Datacard %s\n", m_device->c_str());
                m_device->disconnect();
                m_device->m_mutex.unlock();
                return;
            }
            else
            {
                m_device->m_mutex.unlock();
                continue;
            }
        }
    
        m_device->m_mutex.lock();

        if (m_device->at_read())
        {
//		goto e_cleanup;
            m_device->disconnect();
            m_device->m_mutex.unlock();
            return;
        }
        while ((iovcnt = m_device->at_read_result_iov()) > 0)
        {
            at_res = m_device->at_read_result_classification(iovcnt);
	    
            if (m_device->at_response(iovcnt, at_res))
            {
//		goto e_cleanup;
                m_device->disconnect();
                m_device->m_mutex.unlock();
                return;
            }
        }
        m_device->m_mutex.unlock();
    } // End of Main loop

//    ast_mutex_lock (&pvt->lock);

//e_cleanup:
//    if (!pvt->initialized)
//    {
//    	ast_verb (3, "Error initializing Datacard %s\n", pvt->id);
//    }

//    disconnect_datacard (pvt);

//    ast_mutex_unlock (&pvt->lock);

}

void MonitorThread::cleanup()
{
}



CardDevice::CardDevice(String name, DevicesEndPoint* ep):String(name), m_endpoint(ep), m_monitor(0), m_mutex(true), m_conn(0), m_connected(false)
{
    m_data_fd = -1;
    m_audio_fd = -1;
    
    /* set some defaults */
    timeout = 10000;
    cusd_use_ucs2_decoding =  1;
    gsm_reg_status = -1;

    m_provider_name = "NONE";
    m_number = "Unknown";

    reset_datacard =  1;
    u2diag = -1;
    callingpres = -1;
    
    m_atQueue.clear();

}
bool CardDevice::startMonitor() 
{ 
    //TODO: Running flag
    m_running = true;
    MonitorThread* m_monitor = new MonitorThread(this);
    return m_monitor->startup();
//    return true;
}

bool CardDevice::tryConnect()
{
    m_mutex.lock();
    if (!m_connected)
    {
	Debug("tryConnect",DebugAll,"Datacard %s trying to connect on %s...\n", safe(), data_tty.safe());
	if ((m_data_fd = opentty((char*)data_tty.safe())) > -1)
	{
		if ((m_audio_fd = opentty((char*)audio_tty.safe())) > -1)
		{
		    if (startMonitor())
		    {
			m_connected = true;
			Debug("tryConnect",DebugAll,"Datacard %s has connected, initializing...\n", safe());
		    }
		}
	}
    }
    m_mutex.unlock();
    return m_connected;
}

bool CardDevice::disconnect()
{
    if(!m_connected)
    {
    	Debug("disconnect",DebugAll,"[%s] Datacard not connected\n", safe());	
    	return m_connected;
    }
    if(isRunning()) 
	stopRunning();

//    if (pvt->owner)
//    {
//    	Debug("disconnect",DebugAll,"[%s] Datacard disconnected, hanging up owner\n", pvt->id);
//		pvt->needchup = 0;
//		channel_queue_hangup (pvt, 0);
//    }

    close(m_data_fd);
    close(m_audio_fd);

    m_data_fd = -1;
    m_audio_fd = -1;

    m_connected	= false;
//    initialized = 0;
//    gsm_registered = 0;

//    incoming = 0;
//    outgoing = 0;
//    needring = 0;
//    needchup = 0;
	
//    gsm_reg_status = -1;

//	pvt->manufacturer[0]	= '\0';
//	pvt->model[0]		= '\0';
//	pvt->firmware[0]	= '\0';
//	pvt->imei[0]		= '\0';
//	pvt->imsi[0]		= '\0';

//	ast_copy_string (pvt->provider_name,	"NONE",		sizeof (pvt->provider_name));
//	ast_copy_string (pvt->number,		"Unknown",	sizeof (pvt->number));

//	rb_init (&pvt->d_read_rb, pvt->d_read_buf, sizeof (pvt->d_read_buf));

	m_atQueue.clear();

	Debug("disconnect",DebugAll,"Datacard %s has disconnected", c_str());
	return m_connected;
}

int CardDevice::devStatus(int fd)
{
    struct termios t;
    if (fd < 0)
	return -1;
    return tcgetattr(fd, &t);
}

//TODO: Do we need syncronization?
bool CardDevice::isRunning() const
{
    bool running;
    
    // m_mutex.lock();
    running = m_running;
    // m_mutex.unlock();
    
    return running;
}

void CardDevice::stopRunning()
{
    // m_mutex.lock();
    m_running = false;
    // m_mutex.unlock();
}

// SMS and USSD
bool CardDevice::sendSMS(const String &called, const String &sms)
{
    Debug(DebugAll, "[%s] sendSMS: %s\n", c_str(), sms.c_str());
    
    Lock lock(m_mutex);
    
    // TODO: Check called & sms
    // Check if msg will be desrtoyed
    char *msg;

    if (m_connected && initialized && gsm_registered)
    {
        if (has_sms)
        {
            msg = strdup(sms.safe());
            if (at_send_cmgs(called.safe()) || at_fifo_queue_add_ptr(CMD_AT_CMGS, RES_SMS_PROMPT, msg))
            {
                free(msg);
                Debug(DebugAll, "[%s] Error sending SMS message\n", c_str());
                return false;
		    }
	    }
        else
        {
            Debug(DebugAll, "Datacard %s doesn't handle SMS -- SMS will not be sent\n", c_str());
            return false;
        }
    }
    else
    {
        Debug(DebugAll, "Device %s not connected / initialized / registered\n", c_str());
        return false;
    }
    return true;
}

bool CardDevice::sendUSSD(const String &ussd)
{
    Debug(DebugAll, "[%s] sendUSSD: %s\n", c_str(), ussd.c_str());

    Lock lock(m_mutex);
    
    if (m_connected && initialized && gsm_registered)
    {
        if (at_send_cusd(ussd.c_str()) || at_fifo_queue_add(CMD_AT_CUSD, RES_OK))
        {
            Debug(DebugAll, "[%s] Error sending USSD command\n", c_str());
            return false;
        }
    }
    else
    {
        Debug(DebugAll, "Device %s not connected / initialized / registered\n", c_str());
        return false;
    }
    return true;
}


bool CardDevice::incomingCall(const String &caller)
{
    m_conn = m_endpoint->createConnection(this);
    if(!m_conn)
    {
        Debug(DebugAll, "CardDevice::incomingCall error: m_conn is NULL\n");
	return false;
    }
    return true;
}


//EndPoint

DevicesEndPoint::DevicesEndPoint(int interval):Thread("DeviceEndPoint"),m_mutex(true),m_interval(interval),m_run(true)
{
    m_devices.clear();
}
DevicesEndPoint::~DevicesEndPoint()
{
}

void DevicesEndPoint::run()
{
    while(m_run)
    {
	CardDevice* dev = 0;
	m_mutex.lock();
        const ObjList *devicesIter = &m_devices;
	while (devicesIter)
	{
		GenObject* obj = devicesIter->get();
		devicesIter = devicesIter->next();
		if (!obj) continue;	
    		dev = static_cast<CardDevice*>(obj);
    		dev->tryConnect();
	}
	m_mutex.unlock();
	
	if (m_run)
	{
	    Thread::sleep(m_interval);
        }
    }
}

void DevicesEndPoint::cleanup()
{
}    

void DevicesEndPoint::onReceiveUSSD(CardDevice* dev, String ussd)
{
}

void DevicesEndPoint::onReceiveSMS(CardDevice* dev, String caller, String sms)
{
}

bool DevicesEndPoint::sendSMS(CardDevice* dev, const String &called, const String &sms)
{
    if (!dev)
    {
        Debug(DebugAll, "DevicesEndPoint::sendSMS() error: dev is NULL\n");
        return false;
    }
    return dev->sendSMS(called, sms);
}

bool DevicesEndPoint::sendUSSD(CardDevice* dev, const String &ussd)
{
    if (!dev)
    {
        Debug(DebugAll, "DevicesEndPoint::sendUSSD() error: dev is NULL\n");
        return false;
    }
    return dev->sendUSSD(ussd);
}

CardDevice* DevicesEndPoint::appendDevice(String name, NamedList* data)
{
    String audio_tty = data->getValue("audio");
    String data_tty = data->getValue("data");
    
    CardDevice * dev = new CardDevice(name, this);
    dev->data_tty = data_tty;
    dev->audio_tty = audio_tty;
    dev->d_read_rb.rb_init(dev->d_read_buf, sizeof(dev->d_read_buf));
    m_mutex.lock();
    m_devices.append(dev);
    m_mutex.unlock();
    return dev;
}

CardDevice* DevicesEndPoint::findDevice(const String &name)
{
    Lock lock(m_mutex);
    return static_cast<CardDevice*>(m_devices[name]);
}

void DevicesEndPoint::cleanDevices()
{
    CardDevice* dev = 0;
    m_mutex.lock();
    const ObjList *devicesIter = &m_devices;
    while (devicesIter)
    {
	GenObject* obj = devicesIter->get();
	devicesIter = devicesIter->next();
	if (!obj) continue;	
	dev = static_cast<CardDevice*>(obj);
	dev->disconnect();
    }
    m_mutex.unlock();
    m_run = false;
}

Connection* DevicesEndPoint::createConnection(CardDevice* dev, void* usrData)
{
    return 0;
}
void DevicesEndPoint::MakeCall(CardDevice* dev, const String &called)
{
}

Connection::Connection(CardDevice* dev):m_dev(dev)
{
}

bool Connection::onRinging()
{
    return true;
}
bool Connection::onAnswered()
{
    return true;
}
bool Connection::onHangup(int reason)
{
    return true;
}
    
bool Connection::sendAnswer()
{

    m_dev->m_mutex.lock();

    if (m_dev->incoming)
    {
	if (m_dev->at_send_ata() || m_dev->at_fifo_queue_add (CMD_AT_A, RES_OK))
	{
	    Debug(DebugAll, "[%s] Error sending ATA command\n", m_dev->c_str());
	}
	m_dev->answered = 1;
    }
    m_dev->m_mutex.unlock();

    return true;
}
bool Connection::sendHangup()
{
    if (!m_dev)
    {
	Debug(DebugAll, "Asked to hangup channel not connected");
	return false;
    }
    
    CardDevice* tmp = m_dev;
     
    Debug(DebugAll, "[%s] Hanging up device\n", tmp->c_str());

    tmp->m_mutex.lock();

//	if (pvt->a_timer)
//	{
//		ast_timer_close (pvt->a_timer);
//		pvt->a_timer = NULL;
//	}

    if (tmp->needchup)
    {
	if (tmp->at_send_chup() || tmp->at_fifo_queue_add (CMD_AT_CHUP, RES_OK))
	{
		Debug(DebugAll, "[%s] Error sending AT+CHUP command\n", tmp->c_str());
	}
	tmp->needchup = 0;
    }

    tmp->m_conn = NULL;
    tmp->needring = 0;

    m_dev = NULL;

    tmp->m_mutex.unlock();

//	ast_setstate (channel, AST_STATE_DOWN);

    return true;
}

bool Connection::sendDTMF(char digit)
{

    m_dev->m_mutex.lock();
    
    if (m_dev->at_send_dtmf(digit) || m_dev->at_fifo_queue_add (CMD_AT_DTMF, RES_OK))
    {
	Debug(DebugAll, "[%s] Error sending DTMF %c\n", m_dev->c_str(), digit);
	m_dev->m_mutex.unlock();
	return -1;
    }
    Debug(DebugAll, "[%s] Send DTMF %c\n", m_dev->c_str(), digit);
    m_dev->m_mutex.unlock();

    return true;
}

