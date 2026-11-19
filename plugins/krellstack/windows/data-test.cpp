#include "StackData.h"
#include <QCoreApplication>
static void check(bool ok,const char *message){if(!ok)qFatal("%s",message);}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    QStringList hosts;check(WindowsStack::hosts("user@server,server;user@server",hosts)&&hosts.size()==2,"Host deduplication");
    for(const auto *bad:{"-oProxyCommand=bad","a;$(touch bad)","a b","user@host|bad","a,b,c,d,e"}){
        hosts.clear();check(!WindowsStack::hosts(bad,hosts),"Unsafe hosts accepted");
    }
    WindowsStack::Counts counts;QString error;
    const QByteArray rows="{\"kind\":\"container\",\"id\":\"a\",\"state\":\"running\",\"project\":\"one\"}\n{\"kind\":\"container\",\"id\":\"b\",\"state\":\"exited\",\"project\":\"one\"}\n{\"kind\":\"unhealthy\",\"id\":\"a\"}\n{\"kind\":\"image\",\"id\":\"i\"}\n{\"kind\":\"image\",\"id\":\"i\"}\n";
    check(WindowsStack::records(rows,counts,error),"Valid records rejected");
    check(counts.containers==2&&counts.running==1&&counts.images==1&&counts.compose==1&&counts.unhealthy==2,"Totals/deduplication failed");
    check(!WindowsStack::records("error text",counts,error),"Error text counted as containers");
    check(!WindowsStack::records(QByteArray(1024*1024+1,'x'),counts,error),"Oversized response accepted");
    check(WindowsStack::records({},counts,error)&&counts.containers==0,"Valid empty engine rejected");
    const WindowsStack::Source source{"Docker","ok",{}, {2,1,1,1,1,1,0}};
    auto object=QJsonObject{{"sources",QJsonArray{WindowsStack::json(source)}}};
    check(WindowsStack::parse(object).valid,"Snapshot rejected");
    auto row=WindowsStack::json(source);row["running"]=3;
    check(!WindowsStack::parse({{"sources",QJsonArray{row}}}).valid,"Impossible count accepted");
    row["running"]=0.5;check(!WindowsStack::parse({{"sources",QJsonArray{row}}}).valid,"Fractional count accepted");
    check(!WindowsStack::parse({}).valid,"Missing data shown as zero");
    counts={};check(WindowsStack::pods("{\"items\":[{\"metadata\":{\"uid\":\"1\"},\"status\":{\"phase\":\"Running\",\"conditions\":[{\"type\":\"Ready\",\"status\":\"True\"}]}},{\"metadata\":{\"uid\":\"2\"},\"status\":{\"phase\":\"Failed\"}}]}",counts),"Pods rejected");
    check(counts.pods==2&&counts.unhealthy==1&&counts.containers==0,"Pods mixed into container counts");
    qInfo("PASS schema, counts, unique images/projects, SSH validation, errors and Kubernetes pods");
}
