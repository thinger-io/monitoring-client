: ' Create backups of thinger, mongodb and influxdb and uploads them
    to an AWS S3 bucket with the name `hostname`_`year-month-day`.

    The backups for mongodb and influx db are created with their own tools.
    As input arguments it needs the s3 access key and s3 secret key
'

usage() {
    echo "usage: thinger_backup.sh -a S3_ACCESS_KEY -s S3_SECRET_KEY -b BUCKET -r BUCKET_REGION [-h]"
    echo
    echo "Backups thinger, mongodb and influxdb and uploads it o an S3 bucket"
    echo
    echo "arguments:"
    echo " -a, --access-key AWS S3 access key"
    echo " -s, --secret-key AWS S3 secret key"
    echo " -b, --bucket   AWS S3 bucket name"
    echo " -r, --region   AWS S3 bucket region"
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
    -h | --help | * )
        usage
        ;;
esac; shift; done

if [ -z "${s3_access_key+x}" ] || [ -z "${s3_secret_key+x}" ] ||
   [ -z "${bucket+x}" ] || [ -z "${region+x}" ]; then
    echo "Missing parameters";
    usage
fi

backup_date=$(date --iso-8601)
backup_folder=/backup
file_to_upload=$(hostname)"_${backup_date}.tar.gz"

# create backup folder
rm -rf "${backup_folder}"
mkdir -p "${backup_folder}"

# backup thinger
cp -rf /data/thinger/users "${backup_folder}/thinger-${backup_date}"
#tar cfz "${backup_folder}/thinger-${backup_date}.tar.gz" -C /data thinger

# backup mongodb
mongo_pwd=$(cat docker-compose.yml | grep MONGO_INITDB_ROOT_PASSWORD | cut -d\= -f2)
docker exec -it mongodb mongodump -u "thinger" -p "${mongo_pwd}"
docker cp mongodb:dump /backup/mongodbdump-$(date --iso-8601)

# backup influxdb
docker exec -it influxdb influxd backup -portable /dump
docker cp influxdb:dump /backup/influxdbdump-$(date --iso-8601)

# Compress all backups
tar cfz "${file_to_upload}" "${backup_folder}"

# about the file
filepath="/${bucket}/${file_to_upload}"

# metadata
contentType="application/x-compressed-tar"
dateValue=`date -R`
signature_string="PUT\n\n${contentType}\n${dateValue}\n${filepath}"

#prepare signature hash to be sent in Authorization header
signature_hash=`echo -en ${signature_string} | openssl sha1 -hmac ${s3_secret_key} -binary | base64`

# actual curl command to do PUT operation on s3
curl -X PUT -T "${file_to_upload}" \
  -H "Host: ${bucket}.s3.amazonaws.com" \
  -H "Date: ${dateValue}" \
  -H "Content-Type: ${contentType}" \
  -H "Authorization: AWS ${s3_access_key}:${signature_hash}" \
  https://${bucket}.s3-${region}.amazonaws.com/${file_to_upload}

## Clean local and docker
rm "${file_to_upload}"
rm -rf "/${backup_folder}"
docker exec -it mongodb rm -rf /dump
docker exec -it influxdb rm -rf /dump 
