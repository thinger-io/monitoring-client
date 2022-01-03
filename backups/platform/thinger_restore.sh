: ' Restores a backup of thinger, mongodb and influxdb downloaded
    from an AWS S3 bucket with the name `hostname`_`year-month-day`.

    The backups for mongodb and influx db are restored with their own tools.
'

usage() {
    echo "usage: thinger_restore.sh -a S3_ACCESS_KEY -s S3_SECRET_KEY -b BUCKET -r BUCKET_REGION -d BACKUP_DATE [-h]"
    echo
    echo "Restores thinger, mongodb and influxdb from an S3 bucket"
    echo
    echo "arguments:"
    echo " -a, --access-key AWS S3 access key"
    echo " -s, --secret-key AWS S3 secret key"
    echo " -b, --bucket   AWS S3 bucket name"
    echo " -r, --region   AWS S3 bucket region"
    echo " -d, --date   Backup date of restore"
    echo
    echo "optional arguments:"
    echo " -h, --help   shot this help message and exit"

    exit 0

}

while [[ "$#" -gt 0 ]]; do case $1 in
    -a | --access-key )
        shift; s3_access_key=$1
        ;;
    -s | --secret-key )
        shift; s3_secret_key=$1
        ;;
    -b | --bucket )
        shift; bucket=$1
        ;;
    -r | --region )
        shift; region=$1
        ;;
    -d | --backup-date )
        shift; backup_date=$1
        ;;
    -h | --help | * )
        usage
        ;;
esac; shift; done

if [ -z "${s3_access_key+x}" ] || [ -z "${s3_secret_key+x}" ] ||
   [ -z "${bucket+x}" ] || [ -z "${region+x}" ] || [ -z "${backup_date+x}" ]; then
    echo "Missing parameters";
    usage
fi

backup_folder=/backup
file_to_download=$(hostname)"_${backup_date}.tar.gz"

# create backup folder
mkdir -p "${backup_folder}"

# about the file
filepath="/${bucket}/${file_to_download}"

# metadata
contentType="application/x-compressed-tar"
dateValue=`date -R`
signature_string="GET\n\n${contentType}\n${dateValue}\n${filepath}"

#prepare signature hash to be sent in Authorization header
signature_hash=`echo -en ${signature_string} | openssl sha1 -hmac ${s3_secret_key} -binary | base64`

# actual curl command to do GET operation on s3
curl -X GET \
  -H "Host: ${bucket}.s3.${region}.amazonaws.com" \
  -H "Date: ${dateValue}" \
  -H "Content-Type: ${contentType}" \
  -H "Authorization: AWS ${s3_access_key}:${signature_hash}" \
  "https://${bucket}.s3-${region}.amazonaws.com/${file_to_download}" \
  --output "${file_to_download}"

# uncompress all
tar xfz "${file_to_download}" --same-owner

# restore thinger
docker stop thinger
rm -rf /data/thinger/users
cp -rf "${backup_folder}/thinger-${backups_date}/users" /data/thinger/.
#tar xfz "${backup_folder}/thinger-${backup_date}.tar.gz" -C /data/ --same-owner

# restore mongodb
mongo_pwd=$(cat docker-compose.yml | grep MONGO_INITDB_ROOT_PASSWORD | cut -d\= -f2)
docker cp "/backup/mongodbdump-${backup_date}" mongodb:/dump
docker exec -it mongodb mongorestore /dump -u "thinger" -p "${mongo_pwd}"

# restore influxdb
docker cp "/backup/influxdbdump-${backup_date}" influxdb:/dump
docker exec -it influxdb influxd restore -portable /dump

## Clean local and docker
rm "${file_to_download}"
rm -rf "/${backup_folder}"
docker exec -it mongodb rm -rf /dump
docker exec -it influxdb rm -rf /dump

docker-compose restart
