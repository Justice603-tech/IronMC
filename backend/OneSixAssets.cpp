#include <QString>
#include <QDebug>
#include <QtXml/QtXml>
#include "OneSixAssets.h"
#include "dlqueue.h"

inline QDomElement getDomElementByTagName(QDomElement parent, QString tagname)
{
	QDomNodeList elementList = parent.elementsByTagName(tagname);
	if (elementList.count())
		return elementList.at(0).toElement();
	else
		return QDomElement();
}

class ThreadedDeleter : public QThread
{
	Q_OBJECT
public:
	void run()
	{
		QDirIterator iter(m_base, QDirIterator::Subdirectories);
		QStringList nuke_list;
		int base_length = m_base.length();
		while (iter.hasNext())
		{
			QString filename = iter.next();
			QFileInfo current(filename);
			// we keep the dirs... whatever
			if(current.isDir())
				continue;
			QString trimmedf = filename;
			trimmedf.remove(0, base_length + 1);
			if(m_whitelist.contains(trimmedf))
			{
				//qDebug() << trimmedf << " gets to live";
			}
			else
			{
				// DO NOT TOLERATE JUNK
				//qDebug() << trimmedf << " dies";
				QFile f (filename);
				f.remove();
			}
		}
	};
	QString m_base;
	QStringList m_whitelist;
};

class NukeAndPaveJob: public Job
{
	Q_OBJECT
public:

	explicit NukeAndPaveJob(QString base, QStringList whitelist)
		:Job()
	{
		QDir dir(base);
		deleterThread.m_base = dir.absolutePath();
		deleterThread.m_whitelist = whitelist;
	};
public slots:
	virtual void start()
	{
		connect(&deleterThread, SIGNAL(finished()), SLOT(threadFinished()));
		deleterThread.start();
	};
	void threadFinished()
	{
		emit finish();
	}
private:
	ThreadedDeleter deleterThread;
};

class Private
{
public:
	JobListQueue dl;
	JobListPtr index_job;
	JobListPtr files_job;
};

OneSixAssets::OneSixAssets(QObject* parent):QObject(parent), d(new Private) {}

void OneSixAssets::fetchFinished()
{
	QString prefix ( "http://s3.amazonaws.com/Minecraft.Resources/" );
	QString fprefix ( "assets/" );
	QStringList nuke_whitelist;

	JobPtr firstJob = d->index_job->getFirstJob();
	auto DlJob = firstJob.dynamicCast<DownloadJob>();
	QByteArray ba = DlJob->m_data;

	QString xmlErrorMsg;
	QDomDocument doc;
	if ( !doc.setContent ( ba, false, &xmlErrorMsg ) )
	{
		qDebug() << "Failed to process s3.amazonaws.com/Minecraft.Resources. XML error:" <<
		         xmlErrorMsg << ba;
	}
	//QRegExp etag_match(".*([a-f0-9]{32}).*");
	QDomNodeList contents = doc.elementsByTagName ( "Contents" );

	JobList *job = new JobList();
	connect ( job, SIGNAL ( finished() ), SIGNAL(finished()) );
	connect ( job, SIGNAL ( failed() ), SIGNAL(failed()) );

	for ( int i = 0; i < contents.length(); i++ )
	{
		QDomElement element = contents.at ( i ).toElement();

		if ( element.isNull() )
			continue;

		QDomElement keyElement = getDomElementByTagName ( element, "Key" );
		QDomElement lastmodElement = getDomElementByTagName ( element, "LastModified" );
		QDomElement etagElement = getDomElementByTagName ( element, "ETag" );
		QDomElement sizeElement = getDomElementByTagName ( element, "Size" );

		if ( keyElement.isNull() || lastmodElement.isNull() || etagElement.isNull() || sizeElement.isNull() )
			continue;

		QString keyStr = keyElement.text();
		QString lastModStr = lastmodElement.text();
		QString etagStr = etagElement.text();
		QString sizeStr = sizeElement.text();

		//Filter folder keys
		if ( sizeStr == "0" )
			continue;

		QString trimmedEtag = etagStr.remove ( '"' );
		job->add ( DownloadJob::create ( QUrl ( prefix + keyStr ),fprefix + keyStr, trimmedEtag ) );
		nuke_whitelist.append ( keyStr );
	}
	job->add ( JobPtr ( new NukeAndPaveJob ( fprefix, nuke_whitelist ) ) );
	d->files_job.reset ( job );
	d->dl.enqueue ( d->files_job );
}
void OneSixAssets::fetchStarted()
{
	qDebug() << "Started downloading!";
}
void OneSixAssets::start()
{
	JobList *job = new JobList();
	job->add ( DownloadJob::create ( QUrl ( "http://s3.amazonaws.com/Minecraft.Resources/" ) ) );
	connect ( job, SIGNAL ( finished() ), SLOT ( fetchFinished() ) );
	connect ( job, SIGNAL ( started() ), SLOT ( fetchStarted() ) );
	d->index_job.reset ( job );
	d->dl.enqueue ( d->index_job );
}

#include "OneSixAssets.moc"